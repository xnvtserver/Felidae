"use strict";
// Turns the repository's .fx sources into supervised training examples.
//
// The supervision is free: at every identifier the corpus actually writes, the
// token that appears IS the correct completion, and every other in-scope name
// is a negative. Same for named arguments - the key actually written next is
// the positive among the call's remaining parameters. No hand labelling, and
// the labels are exactly the decision the extension has to make at runtime.

const fs = require("fs");
const path = require("path");
const { completionFeatures, nextParamFeatures } = require("./features");

const IDENT = /[A-Za-z_][A-Za-z0-9_]*/y;

// Mirrors the extensions' own masking: string bodies and `#` comments must not
// contribute identifiers or bracket structure.
function maskLine(line) {
  let masked = "";
  let inString = false;
  let i = 0;
  while (i < line.length) {
    const ch = line[i];
    if (inString) {
      if (ch === "\\" && i + 1 < line.length) { masked += "xx"; i += 2; continue; }
      if (ch === '"') { inString = false; masked += '"'; i++; continue; }
      masked += "x"; i++; continue;
    }
    if (ch === "#") { masked += " ".repeat(line.length - i); break; }
    if (ch === '"') { inString = true; masked += '"'; i++; continue; }
    masked += ch; i++;
  }
  return masked;
}

const DECLARATION =
  /^[ \t]*([A-Za-z_][A-Za-z0-9_:.]*)(?:\s+extend\s+([A-Za-z_][A-Za-z0-9_]*))?\s*\(([\s\S]*?)\)\s*(=>|\.)/gm;

function collectHeadParams(argsText) {
  const params = [];
  const seen = new Set();
  let depth = 0;
  let start = 0;
  const flush = (end) => {
    const segment = argsText.slice(start, end).trim();
    const match = /^([A-Za-z_][A-Za-z0-9_]*)\s*:(?!=)/.exec(segment);
    if (match && !seen.has(match[1])) {
      seen.add(match[1]);
      params.push(match[1]);
    }
  };
  for (let i = 0; i < argsText.length; i++) {
    const ch = argsText[i];
    if (ch === "(" || ch === "{" || ch === "[") depth++;
    else if (ch === ")" || ch === "}" || ch === "]") depth = Math.max(0, depth - 1);
    else if (ch === "," && depth === 0) { flush(i); start = i + 1; }
  }
  flush(argsText.length);
  return params;
}

function listFxFiles(roots) {
  const files = [];
  const walk = (dir) => {
    let entries;
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const entry of entries) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        if (entry.name === "node_modules" || entry.name.startsWith(".")) continue;
        walk(full);
      } else if (entry.isFile() && entry.name.endsWith(".fx")) {
        files.push(full);
      }
    }
  };
  for (const root of roots) walk(root);
  return files.sort();
}

/** Declarations and library names visible across the whole corpus. */
function buildCorpusIndex(files, builtinDocs) {
  const builtinFreq = Object.create(null);
  const slotFreq = Object.create(null);
  const declarations = new Map(); // name -> params[]
  const libraryNames = new Set();

  for (const key of Object.keys(builtinDocs)) {
    const base = key.split(":")[0];
    libraryNames.add(base);
    const leaf = key.includes(":") ? key.split(":").pop() : key;
    libraryNames.add(leaf);
  }

  for (const file of files) {
    const text = fs.readFileSync(file, "utf8").replace(/\r\n/g, "\n");
    const masked = text.split("\n").map(maskLine).join("\n");

    const declaration = new RegExp(DECLARATION);
    let match;
    while ((match = declaration.exec(masked)) !== null) {
      declarations.set(match[1], collectHeadParams(match[3]));
    }

    for (const token of masked.match(/[A-Za-z_][A-Za-z0-9_]*/g) || []) {
      if (libraryNames.has(token)) builtinFreq[token] = (builtinFreq[token] || 0) + 1;
    }
  }

  // How often each named argument appears at each argument slot, over calls
  // whose parameters we know. Gives the next-param model real prior structure.
  for (const file of files) {
    const text = fs.readFileSync(file, "utf8").replace(/\r\n/g, "\n");
    for (const call of iterateCalls(text.split("\n").map(maskLine).join("\n"))) {
      call.suppliedKeys.forEach((key, slot) => {
        const slotKey = key + "@" + Math.min(slot, 8);
        slotFreq[slotKey] = (slotFreq[slotKey] || 0) + 1;
      });
    }
  }

  return { builtinFreq, slotFreq, declarations, libraryNames };
}

/** Yields every call site with the ordered keys it supplies. */
function* iterateCalls(maskedText) {
  const namePattern = /([A-Za-z_][A-Za-z0-9_.:]*)\s*\(/g;
  let match;
  while ((match = namePattern.exec(maskedText)) !== null) {
    const open = match.index + match[0].length - 1;
    let depth = 0;
    let end = -1;
    for (let i = open; i < maskedText.length; i++) {
      const ch = maskedText[i];
      if (ch === "(" || ch === "{" || ch === "[") depth++;
      else if (ch === ")" || ch === "}" || ch === "]") {
        depth--;
        if (depth === 0) { end = i; break; }
      }
    }
    if (end < 0) continue;
    const body = maskedText.slice(open + 1, end);
    const suppliedKeys = [];
    let segmentDepth = 0;
    let segmentStart = 0;
    const flush = (stop) => {
      const segment = body.slice(segmentStart, stop);
      const key = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:(?!=)/.exec(segment);
      if (key) suppliedKeys.push(key[1]);
    };
    for (let i = 0; i < body.length; i++) {
      const ch = body[i];
      if (ch === "(" || ch === "{" || ch === "[") segmentDepth++;
      else if (ch === ")" || ch === "}" || ch === "]") segmentDepth--;
      else if (ch === "," && segmentDepth === 0) { flush(i); segmentStart = i + 1; }
    }
    flush(body.length);
    if (suppliedKeys.length === 0) continue;
    yield { name: match[1].replace(/\./g, ":"), suppliedKeys, open, end };
  }
}

/**
 * Completion-ranking examples. At each identifier occurrence, the written
 * token is the positive and the other in-scope candidates are negatives.
 */
function buildCompletionExamples(files, index, options = {}) {
  const negativesPerSite = options.negativesPerSite || 12;
  const groups = [];
  let seed = 12345;
  const rand = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff;

  for (const file of files) {
    const text = fs.readFileSync(file, "utf8").replace(/\r\n/g, "\n");
    const lines = text.split("\n").map(maskLine);

    // In-scope candidate pool for this file.
    const candidates = new Map(); // name -> kind
    const declaration = new RegExp(DECLARATION);
    const joined = lines.join("\n");
    let match;
    while ((match = declaration.exec(joined)) !== null) {
      candidates.set(match[1], match[4] === "=>" ? "method" : "fact");
      for (const param of collectHeadParams(match[3])) {
        if (!candidates.has(param)) candidates.set(param, "param");
      }
    }
    for (const binding of joined.match(/\b([A-Za-z_][A-Za-z0-9_]*)\s*:=/g) || []) {
      const name = binding.replace(/\s*:=$/, "").trim();
      if (!candidates.has(name)) candidates.set(name, "local");
    }
    for (const name of index.libraryNames) {
      if (!candidates.has(name)) candidates.set(name, "library");
    }
    if (candidates.size < 3) continue;

    const localCounts = Object.create(null);
    const lastUseLine = Object.create(null);
    const pool = [...candidates.keys()];

    for (let lineNumber = 0; lineNumber < lines.length; lineNumber++) {
      IDENT.lastIndex = 0;
      const line = lines[lineNumber];
      let token;
      let cursor = 0;
      while (cursor < line.length) {
        IDENT.lastIndex = cursor;
        token = IDENT.exec(line);
        if (!token) { cursor++; continue; }
        const name = token[0];
        cursor = IDENT.lastIndex;

        if (candidates.has(name) && name.length >= 2) {
          // Simulate the user having typed the first character or two.
          for (const prefixLen of [0, 1, Math.min(2, name.length - 1)]) {
            const prefix = name.slice(0, prefixLen);
            const context = { prefix, localCounts, lastUseLine, line: lineNumber };
            const rows = [
              {
                label: 1,
                features: completionFeatures({ name, kind: candidates.get(name) }, context, index)
              }
            ];
            let added = 0;
            for (let attempt = 0; attempt < pool.length && added < negativesPerSite; attempt++) {
              const other = pool[Math.floor(rand() * pool.length)];
              if (other === name) continue;
              if (prefix && !other.toLowerCase().startsWith(prefix.toLowerCase())) {
                // Keep some hard negatives that share the prefix, plus a few
                // easy ones, so the model learns more than "prefix matches".
                if (rand() > 0.35) continue;
              }
              rows.push({
                label: 0,
                features: completionFeatures({ name: other, kind: candidates.get(other) }, context, index)
              });
              added++;
            }
            // Shuffle so the positive is not always first: rankingMetrics
            // sorts stably, so an unshuffled group would let a constant
            // scorer "win" purely by reading back the construction order.
            for (let i = rows.length - 1; i > 0; i--) {
              const j = Math.floor(rand() * (i + 1));
              const swap = rows[i];
              rows[i] = rows[j];
              rows[j] = swap;
            }
            if (rows.length > 1) groups.push({ rows });
          }
        }

        localCounts[name] = (localCounts[name] || 0) + 1;
        lastUseLine[name] = lineNumber;
      }
    }
  }
  return groups;
}

/** Next-parameter examples: which key follows the ones already written. */
function buildNextParamExamples(files, index, builtinDocs) {
  const groups = [];
  const paramsFor = (callName) => {
    const builtin = builtinDocs[callName];
    if (builtin && builtin.params && builtin.params.length) {
      return { params: builtin.params.map((p) => p.name), builtin: true };
    }
    const declared = index.declarations.get(callName)
      || index.declarations.get(callName.split(":").pop());
    return declared ? { params: declared, builtin: false } : null;
  };

  for (const file of files) {
    const text = fs.readFileSync(file, "utf8").replace(/\r\n/g, "\n");
    const masked = text.split("\n").map(maskLine).join("\n");
    for (const call of iterateCalls(masked)) {
      const resolved = paramsFor(call.name);
      if (!resolved || resolved.params.length < 2) continue;

      for (let supplied = 0; supplied < call.suppliedKeys.length; supplied++) {
        const target = call.suppliedKeys[supplied];
        if (!resolved.params.includes(target)) continue;
        const already = new Set(call.suppliedKeys.slice(0, supplied));
        const remaining = resolved.params.filter((name) => !already.has(name));
        if (remaining.length < 2) continue;

        const firstUnsuppliedIndex = resolved.params.findIndex((name) => !already.has(name));
        const context = {
          paramsTotal: resolved.params.length,
          suppliedCount: supplied,
          firstUnsuppliedIndex,
          isBuiltinCall: resolved.builtin
        };
        const rows = remaining.map((name) => ({
          label: name === target ? 1 : 0,
          features: nextParamFeatures(
            { name, declIndex: resolved.params.indexOf(name) },
            context,
            index
          )
        }));
        // Left in declaration order on purpose: the baseline this model has to
        // beat IS declaration order, and rankingMetrics sorts stably, so a
        // constant scorer reproduces exactly that baseline.
        if (rows.some((row) => row.label === 1)) groups.push({ rows });
      }
    }
  }
  return groups;
}

module.exports = {
  listFxFiles,
  buildCorpusIndex,
  buildCompletionExamples,
  buildNextParamExamples,
  iterateCalls,
  collectHeadParams,
  maskLine
};
