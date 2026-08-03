// Folding ranges for Felidae v2 sources.
//
//   node test/folding.test.js [file.fx]
//
// This used to print the ranges it found and exit 0 regardless, which meant
// it passed just as happily when the provider returned nothing at all - and
// returning nothing is exactly the bug it existed to catch, back when folding
// was still anchored on the v1 terminating dot. The cases below assert
// specific ranges against sources whose correct folding is known by reading
// them; the optional file argument keeps the old smoke-check behaviour.

"use strict";

const path = require("path");
const fs = require("fs");
const Module = require("module");

const stub = path.resolve(__dirname, "vscode-stub.js");
const originalResolve = Module._resolveFilename;
Module._resolveFilename = function (request, ...rest) {
  if (request === "vscode") return stub;
  if (/^vscode-languageclient/.test(request)) return path.resolve(__dirname, "lc-stub.js");
  return originalResolve.call(this, request, ...rest);
};

const vscode = require(stub);
vscode.FoldingRange = class { constructor(start, end, kind) { this.start = start; this.end = end; this.kind = kind; } };
vscode.FoldingRangeKind = { Region: "Region", Comment: "Comment" };

const extensionPath = path.resolve(__dirname, "..", "out", "extension.js");
const source = fs.readFileSync(extensionPath, "utf8") +
  "\nmodule.exports.__fold=FelidaeFoldingRangeProvider;";
const loaded = new Module(extensionPath);
loaded.filename = extensionPath;
loaded.paths = Module._nodeModulePaths(path.dirname(extensionPath));
loaded._compile(source, extensionPath);

const provider = new (loaded.exports.__fold)();

function documentOf(text) {
  const lines = text.split("\n");
  return {
    languageId: "felidae",
    lineCount: lines.length,
    lineAt: n => ({ text: lines[n] }),
    getText: () => text
  };
}

let failures = 0;
let checks = 0;

function check(condition, message, detail) {
  checks++;
  if (condition) {
    console.log("  ok    " + message);
  } else {
    failures++;
    console.log("  FAIL  " + message + (detail ? "  (" + detail + ")" : ""));
  }
}

// Ranges are reported zero-based; the expectations below are written
// one-based to match what an editor shows, and converted here.
function regions(text, kind) {
  return provider.provideFoldingRanges(documentOf(text))
    .filter(range => !kind || range.kind === kind)
    .map(range => [range.start + 1, range.end + 1])
    .sort((a, b) => a[0] - b[0]);
}

function sameRanges(actual, expected) {
  return JSON.stringify(actual) === JSON.stringify(expected);
}

// --------------------------------------------------------------------------
console.log("multi-line method body");
{
  const text = [
    "Greeting(name: string)",              // 1  one-liner: nothing to fold
    "",                                    // 2
    "greet(name: string) =>",              // 3  region 3-6
    "    Greeting(name: name)",            // 4
    "    system.print(value: name)",       // 5
    "    return",                          // 6
    "",                                    // 7
    "main() =>",                           // 8  region 8-9
    "    greet(name: \"a\")"               // 9
  ].join("\n");
  const found = regions(text, "Region");
  check(sameRanges(found, [[3, 6], [8, 9]]),
    "folds each multi-line declaration and nothing else", JSON.stringify(found));
}

// --------------------------------------------------------------------------
console.log("v2 sources have no terminating dot");
{
  // The whole reason this suite exists: folding was written against v1's
  // trailing '.' and produced zero regions on every v2 file in the repo.
  const text = [
    "compute(x: number) =>",
    "    y := x + 1",
    "    return y"
  ].join("\n");
  const found = regions(text, "Region");
  check(found.length === 1 && found[0][0] === 1 && found[0][1] === 3,
    "a dot-free v2 declaration still folds", JSON.stringify(found));
}

// --------------------------------------------------------------------------
console.log("extend clauses");
{
  const text = [
    "Person(name: string)",                // 1
    "Employee extend Person(name: n) =>",  // 2  region 2-3
    "    return",                          // 3
    "Manager extend Employee(name: n)"     // 4  starts a new construct
  ].join("\n");
  const found = regions(text, "Region");
  check(sameRanges(found, [[2, 3]]),
    "`extend` starts a construct rather than joining the previous one",
    JSON.stringify(found));
}

// --------------------------------------------------------------------------
console.log("trailing comments belong to what follows");
{
  const text = [
    "first(x: number) =>",   // 1  region 1-2, NOT 1-4
    "    return x",          // 2
    "",                      // 3
    "# documents `second`",  // 4
    "second(x: number) =>",  // 5  region 5-6
    "    return x"           // 6
  ].join("\n");
  const found = regions(text, "Region");
  check(sameRanges(found, [[1, 2], [5, 6]]),
    "a declaration does not swallow the next one's doc comment",
    JSON.stringify(found));
}

// --------------------------------------------------------------------------
console.log("comment blocks");
{
  const text = [
    "# line one",     // 1  comment region 1-3
    "# line two",     // 2
    "# line three",   // 3
    "Thing(a: 1)",    // 4
    "# lone comment", // 5  single line: not foldable
    "Other(a: 2)"     // 6
  ].join("\n");
  const found = regions(text, "Comment");
  check(sameRanges(found, [[1, 3]]),
    "consecutive comment lines fold, a single one does not", JSON.stringify(found));
}

// --------------------------------------------------------------------------
console.log("degenerate input");
{
  check(regions("").length === 0, "an empty document yields no ranges");
  check(regions("\n\n\n").length === 0, "blank lines alone yield no ranges");
  check(regions("Fact(a: 1)").length === 0, "a single one-line fact is not foldable");
  const wrongLanguage = provider.provideFoldingRanges(
    Object.assign(documentOf("a(x: 1) =>\n    return"), { languageId: "plaintext" }));
  check(wrongLanguage.length === 0, "a non-Felidae document yields no ranges");
}

// --------------------------------------------------------------------------
// Smoke check against a real corpus file, when one is given. A v2 source of
// any size must produce at least one region; zero would mean the provider has
// regressed to not recognising the syntax at all.
if (process.argv[2]) {
  const file = process.argv[2];
  console.log("corpus file: " + path.basename(file));
  const text = fs.readFileSync(file, "utf8").replace(/\r\n/g, "\n");
  const found = provider.provideFoldingRanges(documentOf(text));
  check(found.length > 0, "produces fold regions for " + path.basename(file),
    found.length + " regions");
  // Every range must be within the document and non-degenerate, or VS Code
  // silently discards it.
  const lineCount = text.split("\n").length;
  const invalid = found.filter(range =>
    range.start < 0 || range.end >= lineCount || range.end <= range.start);
  check(invalid.length === 0, "every range is in bounds and spans at least two lines",
    invalid.length + " invalid");
}

console.log(failures === 0
  ? checks + " checks passed"
  : failures + " of " + checks + " checks FAILED");
process.exit(failures === 0 ? 0 : 1);
