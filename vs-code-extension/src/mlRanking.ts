import * as fs from "fs";
import * as path from "path";

// Runtime scoring for the ranking models trained offline in ml/ (gradient
// boosted decision trees, trained with ml-cart via ml/lib/gbdt.js on this
// repository's own .fx corpus).
//
// Only the evaluator lives here: no ML library ships inside the extension, and
// the model is a small JSON tree ensemble loaded once from resources/. The
// IntelliJ plugin embeds the byte-identical logic in
// FelidaeMlRanking.java/FelidaeGbdt.java - keep the three in sync, and keep
// the feature vectors identical to ml/lib/features.js or the model scores
// noise. ml/verify-parity.js checks exactly that.

interface GbdtNode {
  // Internal node.
  f?: number;
  t?: number;
  l?: GbdtNode;
  r?: GbdtNode;
  // Leaf.
  v?: number;
}

interface GbdtModel {
  task: string;
  featureNames: string[];
  objective: "logistic" | "l2";
  base: number;
  learningRate: number;
  trees: GbdtNode[];
  metrics?: Record<string, number>;
}

interface CorpusIndex {
  builtinFreq: Record<string, number>;
  slotFreq: Record<string, number>;
}

export type CandidateKind = "local" | "param" | "method" | "fact" | "library";

// ---------------------------------------------------------------------------
// Tree evaluation. Mirrors ml/lib/gbdt.js exactly, including the strict `<`
// (ml-cart sends a sample left when x[f] < threshold).
// ---------------------------------------------------------------------------

function evalTree(node: GbdtNode, featureVector: number[]): number {
  let current = node;
  while (current.v === undefined) {
    const value = featureVector[current.f as number] ?? 0;
    current = (value < (current.t as number) ? current.l : current.r) as GbdtNode;
  }
  return current.v;
}

function sigmoid(x: number): number {
  if (x >= 0) return 1 / (1 + Math.exp(-x));
  const z = Math.exp(x);
  return z / (1 + z);
}

function predict(model: GbdtModel, featureVector: number[]): number {
  let score = model.base;
  for (const tree of model.trees) score += model.learningRate * evalTree(tree, featureVector);
  return model.objective === "logistic" ? sigmoid(score) : score;
}

// ---------------------------------------------------------------------------
// Model loading. Absent or unreadable models simply disable ranking, so the
// extension keeps working exactly as it did before ML was introduced.
// ---------------------------------------------------------------------------

const MAX_NAME_LEN = 32;
const RECENCY_WINDOW = 60;

let loaded = false;
let completionModel: GbdtModel | undefined;
let nextParamModel: GbdtModel | undefined;
let corpusIndex: CorpusIndex = { builtinFreq: {}, slotFreq: {} };

function readJson<T>(file: string): T | undefined {
  try {
    return JSON.parse(fs.readFileSync(file, "utf8")) as T;
  } catch {
    return undefined;
  }
}

export function loadModels(extensionPath: string): void {
  if (loaded) return;
  loaded = true;
  const dir = path.join(extensionPath, "resources", "models");
  completionModel = readJson<GbdtModel>(path.join(dir, "completion-rank.json"));
  nextParamModel = readJson<GbdtModel>(path.join(dir, "next-param.json"));
  corpusIndex = readJson<CorpusIndex>(path.join(dir, "corpus-index.json")) ?? {
    builtinFreq: {},
    slotFreq: {}
  };
}

export function isCompletionRankingEnabled(): boolean {
  return completionModel !== undefined;
}

export function isNextParamRankingEnabled(): boolean {
  return nextParamModel !== undefined;
}

// ---------------------------------------------------------------------------
// Feature vectors. These MUST match ml/lib/features.js element for element.
// ---------------------------------------------------------------------------

export interface CompletionContext {
  prefix: string;
  line: number;
  localCounts: Record<string, number>;
  lastUseLine: Record<string, number>;
}

function completionFeatures(
  name: string,
  kind: CandidateKind,
  context: CompletionContext
): number[] {
  const prefix = context.prefix;
  const isPrefixMatch = prefix.length > 0 && name.startsWith(prefix) ? 1 : 0;
  const isPrefixMatchCI =
    prefix.length > 0 && name.toLowerCase().startsWith(prefix.toLowerCase()) ? 1 : 0;

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

export interface NextParamContext {
  paramsTotal: number;
  suppliedCount: number;
  firstUnsuppliedIndex: number;
  isBuiltinCall: boolean;
}

function nextParamFeatures(
  name: string,
  declIndex: number,
  context: NextParamContext
): number[] {
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
export function scoreCompletion(
  name: string,
  kind: CandidateKind,
  context: CompletionContext
): number {
  if (!completionModel) return 0;
  return predict(completionModel, completionFeatures(name, kind, context));
}

/** Probability that `name` is the next named argument to be written. */
export function scoreNextParam(
  name: string,
  declIndex: number,
  context: NextParamContext
): number {
  if (!nextParamModel) return 0;
  return predict(nextParamModel, nextParamFeatures(name, declIndex, context));
}

/**
 * Builds the per-file usage statistics the completion features need, by
 * scanning the text above the cursor. Strings and comments are masked so they
 * contribute neither counts nor recency, matching ml/lib/corpus.js.
 */
export function buildCompletionContext(
  textAboveCursor: string,
  prefix: string
): CompletionContext {
  const localCounts: Record<string, number> = Object.create(null);
  const lastUseLine: Record<string, number> = Object.create(null);
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
export function __predictForParity(model: unknown, featureVector: number[]): number {
  return predict(model as GbdtModel, featureVector);
}

/** Blanks string bodies and `#` comments, preserving length. */
function maskLine(line: string): string {
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
