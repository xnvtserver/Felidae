const path = require("path");
const Module = require("module");
const stubPath = path.resolve(__dirname, "vscode-stub.js");
const origResolve = Module._resolveFilename;
Module._resolveFilename = function (request, ...rest) {
  if (request === "vscode") return stubPath;
  if (/^vscode-languageclient/.test(request)) return require("path").resolve(require("path").resolve(__dirname, "lc-stub.js"));
  return origResolve.call(this, request, ...rest);
};
const vscode = require(stubPath);

const EXT = path.resolve(__dirname, "..", "out", "extension.js");

// extension.js only exports activate/deactivate; re-evaluate it with the
// internals appended to module.exports so the pure helpers are reachable.
const fs = require("fs");
let src = fs.readFileSync(EXT, "utf8");
src += `
module.exports.__test = {
  collectHeadParams, resolveCall, completionsForCallFields,
  enclosingCall, callArgumentState, lexDocument, tokenIndexBefore,
  FelidaeSignatureHelpProvider, FelidaeHoverProvider,
};
`;
const mod = new Module(EXT);
mod.filename = EXT;
mod.paths = Module._nodeModulePaths(path.dirname(EXT));
mod._compile(src, EXT);
const T = mod.exports.__test;

// --- fake TextDocument ------------------------------------------------
function doc(text, file = "c:/tmp/test.fx") {
  const lines = text.split("\n");
  return {
    languageId: "felidae",
    eol: 1,
    lineCount: lines.length,
    uri: vscode.Uri.file(file),
    getText: () => text,
    lineAt: (n) => ({ text: lines[n] }),
    positionAt: (off) => {
      let rem = off;
      for (let i = 0; i < lines.length; i++) {
        if (rem <= lines[i].length) return new vscode.Position(i, rem);
        rem -= lines[i].length + 1;
      }
      return new vscode.Position(lines.length - 1, 0);
    },
    offsetAt: (pos) => {
      let off = 0;
      for (let i = 0; i < pos.line; i++) off += lines[i].length + 1;
      return off + pos.character;
    },
  };
}

let pass = 0, fail = 0;
function check(name, actual, expected) {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a === e) { pass++; console.log(`  ok  ${name}`); }
  else { fail++; console.log(`  FAIL ${name}\n       expected ${e}\n       actual   ${a}`); }
}

console.log("collectHeadParams:");
check("typed params", T.collectHeadParams("entity: any, corpus: array"),
  [{ name: "entity", type: "any" }, { name: "corpus", type: "array" }]);
check("variable binding has no type... (it does: the var name)",
  T.collectHeadParams("employee: e").map(p => p.name), ["employee"]);
check("nested default stays one param",
  T.collectHeadParams("opts: {a: 1, b: 2}, other: string").map(p => p.name), ["opts", "other"]);
check("ignores := ", T.collectHeadParams("x := 1").map(p => p.name), []);

console.log("\nsignature help (builtin str.concat):");
const d1 = doc('main() =>\n    str.concat(');
const sig = new T.FelidaeSignatureHelpProvider();
const h1 = sig.provideSignatureHelp(d1, new vscode.Position(1, 15));
check("label", h1 && h1.signatures[0].label, "str.concat(left:, right:, result:)");
check("activeParameter at open paren", h1 && h1.activeParameter, 0);

console.log("\nactive parameter follows the typed key (order-independent):");
const d2 = doc('main() =>\n    str.concat(result: x, left: ');
const h2 = sig.provideSignatureHelp(d2, new vscode.Position(1, 32));
check("after 'result:' supplied, typing 'left' highlights left(0)", h2 && h2.activeParameter, 0);

const d3 = doc('main() =>\n    str.concat(left: "a", ');
const h3 = sig.provideSignatureHelp(d3, new vscode.Position(1, 26));
check("after left supplied, next unsupplied is right(1)", h3 && h3.activeParameter, 1);

console.log("\nsupplied-key filtering in completion:");
const tokens = T.lexDocument(d3).tokens;
const idx = T.tokenIndexBefore(tokens, new vscode.Position(1, 26));
const call = T.enclosingCall(tokens, idx);
check("enclosing call name", call && call.name, "str:concat");
const state = T.callArgumentState(tokens, call.openParen, idx);
check("suppliedKeys", [...state.suppliedKeys], ["left"]);
const items = T.completionsForCallFields(d3, call.name, state.suppliedKeys);
check("left no longer suggested", items.map(i => i.label), ["right", "result"]);

console.log("\nuser-defined method via regex fallback (no felidae_debug cache):");
const d4 = doc('Greeting(name: string, times: number) =>\n    return\n\nmain() =>\n    Greeting(');
const h4 = sig.provideSignatureHelp(d4, new vscode.Position(4, 13));
check("user signature", h4 && h4.signatures[0].label, "Greeting(name: string, times: number)");

console.log("\nregression: Fact.all must NOT suggest 'rows' (the := target):");
const d5 = doc('main() =>\n    Fact.all(');
const items5 = T.completionsForCallFields(d5, "Fact:all", new Set());
check("Fact.all params", items5.map(i => i.label), ["type"]);

console.log("\nhover on a user-defined method:");
const hov = new T.FelidaeHoverProvider();
const hres = hov.provideHover(d4, new vscode.Position(0, 3));
check("hover produced", !!hres, true);

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
