"use strict";
// Feature definitions for the Felidae ranking models.
//
// CRITICAL: every feature here is recomputed at runtime inside the editor
// extensions - vs-code-extension/src/mlRanking.ts and
// intellij-idea-extension/.../ml/FelidaeMlRanking.java. All three
// implementations must produce identical vectors or the shipped model scores
// garbage. Keep the feature list small, integer/ratio valued, and dependent
// only on information an extension already has at completion time.
//
// ml/verify-parity.js checks the TS and Java implementations against this one
// on shared fixtures; run it after touching anything in this file.

// ---------------------------------------------------------------------------
// Completion ranking: score one candidate identifier for one completion site.
// ---------------------------------------------------------------------------

const COMPLETION_FEATURES = [
  "prefixLen",        // 0 characters already typed
  "isPrefixMatch",    // 1 candidate starts with the typed prefix (exact case)
  "isPrefixMatchCI",  // 2 ... ignoring case
  "matchRatio",       // 3 prefixLen / candidateLen
  "candLen",          // 4 candidate name length, capped
  "kindLocal",        // 5 one-hot: local `:=` binding
  "kindParam",        // 6 one-hot: parameter or lambda item
  "kindMethod",       // 7 one-hot: user-defined method
  "kindFact",         // 8 one-hot: user-defined fact
  "kindLibrary",      // 9 one-hot: stdlib module or builtin
  "localFreq",        // 10 log1p(times already used earlier in this file)
  "recency",          // 11 1 = used just above the cursor, 0 = never/far
  "builtinFreq"       // 12 log1p(corpus frequency), builtins only
];

const CANDIDATE_KINDS = ["local", "param", "method", "fact", "library"];

const MAX_NAME_LEN = 32;
const RECENCY_WINDOW = 60;

/**
 * @param {object} candidate {name, kind}
 * @param {object} context   {prefix, localCounts, lastUseLine, line}
 * @param {object} corpus    {builtinFreq: {name: count}}
 */
function completionFeatures(candidate, context, corpus) {
  const name = candidate.name;
  const prefix = context.prefix || "";
  const kind = candidate.kind;

  const isPrefixMatch = prefix.length > 0 && name.startsWith(prefix) ? 1 : 0;
  const isPrefixMatchCI =
    prefix.length > 0 && name.toLowerCase().startsWith(prefix.toLowerCase()) ? 1 : 0;

  const localCount = (context.localCounts && context.localCounts[name]) || 0;
  const lastUse = context.lastUseLine && context.lastUseLine[name];
  const distance = lastUse === undefined ? RECENCY_WINDOW : Math.max(0, context.line - lastUse);
  const recency = Math.max(0, 1 - Math.min(distance, RECENCY_WINDOW) / RECENCY_WINDOW);

  const builtinCount = (kind === "library" && corpus.builtinFreq[name]) || 0;

  return [
    Math.min(prefix.length, MAX_NAME_LEN),
    isPrefixMatch,
    isPrefixMatchCI,
    name.length > 0 ? Math.min(prefix.length / name.length, 1) : 0,
    Math.min(name.length, MAX_NAME_LEN),
    kind === "local" ? 1 : 0,
    kind === "param" ? 1 : 0,
    kind === "method" ? 1 : 0,
    kind === "fact" ? 1 : 0,
    kind === "library" ? 1 : 0,
    Math.log1p(localCount),
    recency,
    Math.log1p(builtinCount)
  ];
}

// ---------------------------------------------------------------------------
// Next-parameter ranking: which `key:` comes next in a call being written.
// ---------------------------------------------------------------------------

const NEXT_PARAM_FEATURES = [
  "declIndex",        // 0 position of this param in the declaration
  "paramsTotal",      // 1 how many params the call declares
  "suppliedCount",    // 2 how many keys are already written
  "remainingCount",   // 3 how many are still unsupplied
  "isNextInDeclOrder",// 4 1 if this is the first unsupplied param
  "declIndexGap",     // 5 declIndex - suppliedCount
  "nameLen",          // 6 param name length, capped
  "isBuiltinCall",    // 7 1 if the call is a stdlib builtin
  "slotFreq"          // 8 log1p(corpus count of this name at this slot)
];

/**
 * @param {object} param   {name, declIndex}
 * @param {object} context {paramsTotal, suppliedCount, firstUnsuppliedIndex, isBuiltinCall}
 * @param {object} corpus  {slotFreq: {"name@slot": count}}
 */
function nextParamFeatures(param, context, corpus) {
  const slotKey = param.name + "@" + Math.min(context.suppliedCount, 8);
  const slotCount = corpus.slotFreq[slotKey] || 0;
  return [
    param.declIndex,
    context.paramsTotal,
    context.suppliedCount,
    Math.max(0, context.paramsTotal - context.suppliedCount),
    param.declIndex === context.firstUnsuppliedIndex ? 1 : 0,
    param.declIndex - context.suppliedCount,
    Math.min(param.name.length, MAX_NAME_LEN),
    context.isBuiltinCall ? 1 : 0,
    Math.log1p(slotCount)
  ];
}

module.exports = {
  COMPLETION_FEATURES,
  NEXT_PARAM_FEATURES,
  CANDIDATE_KINDS,
  MAX_NAME_LEN,
  RECENCY_WINDOW,
  completionFeatures,
  nextParamFeatures
};
