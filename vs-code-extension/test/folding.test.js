const path=require("path"),fs=require("fs"),Module=require("module");
const stub=path.resolve(__dirname, "vscode-stub.js");
const orig=Module._resolveFilename;
Module._resolveFilename=function(r,...a){ if(r==="vscode")return stub;
  if(/^vscode-languageclient/.test(r))return path.resolve(__dirname, "lc-stub.js"); return orig.call(this,r,...a); };
const vscode=require(stub);
vscode.FoldingRange=class{constructor(s,e,k){this.start=s;this.end=e;this.kind=k;}};
vscode.FoldingRangeKind={Region:"Region",Comment:"Comment"};
const EXT=path.resolve(__dirname, "..", "out", "extension.js");
let src=fs.readFileSync(EXT,"utf8")+"\nmodule.exports.__fold=FelidaeFoldingRangeProvider;";
const m=new Module(EXT); m.filename=EXT; m.paths=Module._nodeModulePaths(path.dirname(EXT)); m._compile(src,EXT);
const P=new (m.exports.__fold)();
function doc(t){const L=t.split("\n");return{languageId:"felidae",lineCount:L.length,lineAt:n=>({text:L[n]}),getText:()=>t};}
const f=fs.readFileSync(process.argv[2],"utf8").replace(/\r\n/g,"\n");
const r=P.provideFoldingRanges(doc(f));
const L=f.split("\n");
console.log(process.argv[2]+": "+r.length+" fold regions");
for(const x of r.slice(0,6)) console.log("  ["+x.kind+"] "+(x.start+1)+"-"+(x.end+1)+"  "+JSON.stringify(L[x.start].slice(0,52)));
