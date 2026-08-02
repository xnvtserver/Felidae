#!/usr/bin/env node
// Maintains the repo-root docs/builtin-docs.json (the single source of truth
// for stdlib hover-doc content, shared with the IntelliJ plugin) and copies it
// into resources/builtin-docs.json so it gets bundled into the packaged
// extension. Runs at build/package time, same as generate-libraries.js.
//
// `heading`, `description` and `example` are hand-authored. `params` is
// DERIVED here from `example` and rewritten on every run - do not hand-edit
// it. Deriving it once at build time is what lets both editor extensions read
// a structured parameter list instead of each re-parsing `example` with its
// own regex at runtime (the VS Code copy of that regex also mis-read `x` in
// `x := Foo(a: 1)` as a parameter; see extractParams below).
//
// The enriched file is written back to docs/builtin-docs.json so that the
// IntelliJ plugin's own `generateBuiltinDocs` Gradle task - a plain file copy
// of that same source - picks the parameters up with no extra wiring.

"use strict";

const fs = require("fs");
const path = require("path");

// Finds `<name>(` in `example` and returns the index of that `(`, or -1.
// Anchored on the documented call name so an example like
// `rows := Fact.all(type: "Customer")` cannot have its assignment target
// mistaken for the call, and so a non-call entry (e.g. the `system.result`
// value, whose example merely mentions it) yields no parameters at all.
function findCallParen(example, callName) {
  let from = 0;
  for (;;) {
    const at = example.indexOf(callName, from);
    if (at < 0) return -1;
    const before = at === 0 ? "" : example[at - 1];
    // Reject a partial match inside a longer identifier/qualified name.
    if (/[A-Za-z0-9_.:]/.test(before)) {
      from = at + callName.length;
      continue;
    }
    let cursor = at + callName.length;
    while (cursor < example.length && /\s/.test(example[cursor])) cursor++;
    if (example[cursor] === "(") return cursor;
    from = at + callName.length;
  }
}

// Splits the argument list at `openParen` into its top-level, comma-separated
// segments, respecting nesting and string literals.
function argumentSegments(example, openParen) {
  const segments = [];
  let depth = 0;
  let start = openParen + 1;
  let inString = false;
  for (let i = openParen; i < example.length; i++) {
    const ch = example[i];
    if (inString) {
      if (ch === "\\") i++;
      else if (ch === '"') inString = false;
      continue;
    }
    if (ch === '"') {
      inString = true;
    } else if (ch === "(" || ch === "[" || ch === "{") {
      depth++;
    } else if (ch === ")" || ch === "]" || ch === "}") {
      depth--;
      if (depth === 0) {
        segments.push(example.slice(start, i));
        return segments;
      }
    } else if (ch === "," && depth === 1) {
      segments.push(example.slice(start, i));
      start = i + 1;
    }
  }
  return segments; // unbalanced example: return what we found
}

// Derives the named parameters a call accepts, e.g. `left`/`right`/`result`
// from `str.concat(left: "hello", right: " world", result: text)`. Positional
// arguments (`lambda(Person, p => ...)`) contribute nothing, which is correct:
// there is no key to complete or label for them.
function extractParams(heading, example) {
  if (typeof heading !== "string" || typeof example !== "string") return [];
  const openParen = findCallParen(example, heading);
  if (openParen < 0) return [];

  const params = [];
  const seen = new Set();
  for (const segment of argumentSegments(example, openParen)) {
    // `(?!=)` keeps `:=` (a binding, not a named argument) out.
    const match = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:(?!=)/.exec(segment);
    if (!match) continue;
    const name = match[1];
    if (seen.has(name)) continue;
    seen.add(name);
    params.push({ name });
  }
  return params;
}

function main() {
  const repoRoot = path.resolve(__dirname, "..", "..");
  const sourcePath = path.join(repoRoot, "docs", "builtin-docs.json");
  const destPath = path.join(__dirname, "..", "resources", "builtin-docs.json");

  if (!fs.existsSync(sourcePath)) {
    console.error(
      `generate-builtin-docs: ${sourcePath} not found. Leaving resources/builtin-docs.json untouched.`
    );
    process.exit(0);
  }

  let docs;
  try {
    docs = JSON.parse(fs.readFileSync(sourcePath, "utf8"));
  } catch (error) {
    console.error(`generate-builtin-docs: could not parse ${sourcePath}: ${error.message}`);
    process.exit(0);
  }

  let withParams = 0;
  for (const [key, entry] of Object.entries(docs)) {
    if (!entry || typeof entry !== "object") continue;
    const params = extractParams(entry.heading ?? key, entry.example ?? "");
    entry.params = params;
    if (params.length) withParams++;
  }

  const serialized = JSON.stringify(docs, null, 2) + "\n";
  fs.writeFileSync(sourcePath, serialized);
  fs.mkdirSync(path.dirname(destPath), { recursive: true });
  fs.writeFileSync(destPath, serialized);
  console.log(
    `generate-builtin-docs: wrote ${Object.keys(docs).length} entries ` +
      `(${withParams} with params) -> ${sourcePath} and ${destPath}`
  );
}

main();
