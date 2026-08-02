"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.loadModels = loadModels;
exports.isCompletionRankingEnabled = isCompletionRankingEnabled;
exports.isNextParamRankingEnabled = isNextParamRankingEnabled;
exports.scoreCompletion = scoreCompletion;
exports.scoreNextParam = scoreNextParam;
exports.buildCompletionContext = buildCompletionContext;
exports.__predictForParity = __predictForParity;
const fs = __importStar(require("fs"));
const path = __importStar(require("path"));
// ---------------------------------------------------------------------------
// Tree evaluation. Mirrors ml/lib/gbdt.js exactly, including the strict `<`
// (ml-cart sends a sample left when x[f] < threshold).
// ---------------------------------------------------------------------------
function evalTree(node, featureVector) {
    let current = node;
    while (current.v === undefined) {
        const value = featureVector[current.f] ?? 0;
        current = (value < current.t ? current.l : current.r);
    }
    return current.v;
}
function sigmoid(x) {
    if (x >= 0)
        return 1 / (1 + Math.exp(-x));
    const z = Math.exp(x);
    return z / (1 + z);
}
function predict(model, featureVector) {
    let score = model.base;
    for (const tree of model.trees)
        score += model.learningRate * evalTree(tree, featureVector);
    return model.objective === "logistic" ? sigmoid(score) : score;
}
// ---------------------------------------------------------------------------
// Model loading. Absent or unreadable models simply disable ranking, so the
// extension keeps working exactly as it did before ML was introduced.
// ---------------------------------------------------------------------------
const MAX_NAME_LEN = 32;
const RECENCY_WINDOW = 60;
let loaded = false;
let completionModel;
let nextParamModel;
let corpusIndex = { builtinFreq: {}, slotFreq: {} };
function readJson(file) {
    try {
        return JSON.parse(fs.readFileSync(file, "utf8"));
    }
    catch {
        return undefined;
    }
}
function loadModels(extensionPath) {
    if (loaded)
        return;
    loaded = true;
    const dir = path.join(extensionPath, "resources", "models");
    completionModel = readJson(path.join(dir, "completion-rank.json"));
    nextParamModel = readJson(path.join(dir, "next-param.json"));
    corpusIndex = readJson(path.join(dir, "corpus-index.json")) ?? {
        builtinFreq: {},
        slotFreq: {}
    };
}
function isCompletionRankingEnabled() {
    return completionModel !== undefined;
}
function isNextParamRankingEnabled() {
    return nextParamModel !== undefined;
}
function completionFeatures(name, kind, context) {
    const prefix = context.prefix;
    const isPrefixMatch = prefix.length > 0 && name.startsWith(prefix) ? 1 : 0;
    const isPrefixMatchCI = prefix.length > 0 && name.toLowerCase().startsWith(prefix.toLowerCase()) ? 1 : 0;
    const localCount = context.localCounts[name] ?? 0;
    const lastUse = context.lastUseLine[name];
    const distance = lastUse === undefined ? RECENCY_WINDOW : Math.max(0, context.line - lastUse);
    const recency = Math.max(0, 1 - Math.min(distance, RECENCY_WINDOW) / RECENCY_WINDOW);
    const builtinCount = kind === "library" ? corpusIndex.builtinFreq[name] ?? 0 : 0;
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
function nextParamFeatures(name, declIndex, context) {
    const slotKey = `${name}@${Math.min(context.suppliedCount, 8)}`;
    const slotCount = corpusIndex.slotFreq[slotKey] ?? 0;
    return [
        declIndex,
        context.paramsTotal,
        context.suppliedCount,
        Math.max(0, context.paramsTotal - context.suppliedCount),
        declIndex === context.firstUnsuppliedIndex ? 1 : 0,
        declIndex - context.suppliedCount,
        Math.min(name.length, MAX_NAME_LEN),
        context.isBuiltinCall ? 1 : 0,
        Math.log1p(slotCount)
    ];
}
// ---------------------------------------------------------------------------
// Public scoring API.
// ---------------------------------------------------------------------------
/** Probability that `name` is the identifier the user is reaching for. */
function scoreCompletion(name, kind, context) {
    if (!completionModel)
        return 0;
    return predict(completionModel, completionFeatures(name, kind, context));
}
/** Probability that `name` is the next named argument to be written. */
function scoreNextParam(name, declIndex, context) {
    if (!nextParamModel)
        return 0;
    return predict(nextParamModel, nextParamFeatures(name, declIndex, context));
}
/**
 * Builds the per-file usage statistics the completion features need, by
 * scanning the text above the cursor. Strings and comments are masked so they
 * contribute neither counts nor recency, matching ml/lib/corpus.js.
 */
function buildCompletionContext(textAboveCursor, prefix) {
    const localCounts = Object.create(null);
    const lastUseLine = Object.create(null);
    const lines = textAboveCursor.split("\n");
    for (let lineNumber = 0; lineNumber < lines.length; lineNumber++) {
        const masked = maskLine(lines[lineNumber]);
        for (const token of masked.match(/[A-Za-z_][A-Za-z0-9_]*/g) ?? []) {
            localCounts[token] = (localCounts[token] ?? 0) + 1;
            lastUseLine[token] = lineNumber;
        }
    }
    return { prefix, line: Math.max(0, lines.length - 1), localCounts, lastUseLine };
}
/**
 * Test seam for ml/verify-parity.js: scores an explicitly supplied model so the
 * offline pipeline can prove this evaluator reproduces its own predictions.
 * Not used by the extension itself.
 */
function __predictForParity(model, featureVector) {
    return predict(model, featureVector);
}
/** Blanks string bodies and `#` comments, preserving length. */
function maskLine(line) {
    let masked = "";
    let inString = false;
    let i = 0;
    while (i < line.length) {
        const ch = line[i];
        if (inString) {
            if (ch === "\\" && i + 1 < line.length) {
                masked += "xx";
                i += 2;
                continue;
            }
            if (ch === '"') {
                inString = false;
                masked += '"';
                i++;
                continue;
            }
            masked += "x";
            i++;
            continue;
        }
        if (ch === "#") {
            masked += " ".repeat(line.length - i);
            break;
        }
        if (ch === '"') {
            inString = true;
            masked += '"';
            i++;
            continue;
        }
        masked += ch;
        i++;
    }
    return masked;
}
//# sourceMappingURL=mlRanking.js.map