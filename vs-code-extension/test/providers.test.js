const path=require("path"), fs=require("fs"), Module=require("module");
const stub=path.resolve(__dirname, "vscode-stub.js");
const orig=Module._resolveFilename;
Module._resolveFilename=function(r,...a){ if(r==="vscode") return stub; if(/^vscode-languageclient/.test(r)) return require("path").resolve(require("path").resolve(__dirname, "lc-stub.js")); return orig.call(this,r,...a); };
const vscode=require(stub);
vscode.DocumentHighlight=class{constructor(r,k){this.range=r;this.kind=k;}};
vscode.DocumentHighlightKind={Text:"Text"};
vscode.Location=class{constructor(u,r){this.uri=u;this.range=r;}};
vscode.SymbolInformation=class{constructor(n,k,c,l){Object.assign(this,{name:n,kind:k,containerName:c,location:l});}};
const EXT=path.resolve(__dirname, "..", "out", "extension.js");
let src=fs.readFileSync(EXT,"utf8")+`
module.exports.__nav={symbolOccurrences,identifierAt,isTopLevelSymbol,FelidaeDocumentHighlightProvider,FelidaeRenameProvider};`;
const mod=new Module(EXT); mod.filename=EXT; mod.paths=Module._nodeModulePaths(path.dirname(EXT));
mod._compile(src,EXT);
const N=mod.exports.__nav;

function doc(text){const lines=text.split("\n");return{languageId:"felidae",eol:1,lineCount:lines.length,
 uri:vscode.Uri.file("c:/t.fx"),getText:(r)=>{if(!r)return text;const l=lines[r.start.line];return l.slice(r.start.character,r.end.character);},lineAt:n=>({text:lines[n],range:new vscode.Range(new vscode.Position(n,0),new vscode.Position(n,lines[n].length))}),
 positionAt:o=>{let rem=o;for(let i=0;i<lines.length;i++){if(rem<=lines[i].length)return new vscode.Position(i,rem);rem-=lines[i].length+1;}return new vscode.Position(0,0);},
 getWordRangeAtPosition:(p,re)=>{const l=lines[p.line];const m=[...l.matchAll(new RegExp(re,"g"))].find(m=>m.index<=p.character&&p.character<=m.index+m[0].length);
   return m?new vscode.Range(new vscode.Position(p.line,m.index),new vscode.Position(p.line,m.index+m[0].length)):undefined;}};}

let pass=0,fail=0;
const chk=(n,a,e)=>{const A=JSON.stringify(a),E=JSON.stringify(e);
  if(A===E){pass++;console.log("  ok  ",n);}else{fail++;console.log("  FAIL",n,"\n     exp",E,"\n     act",A);}};

const src1=`Employee(name: "Alice", role: "Engineer")
Employee(name: "Bob", role: "Manager")

HasRole(employee: e, role: string) =>
    role == e.role
    return

main() =>
    x := HasRole(employee: "Alice", role: "Engineer")
    return x`;
const d=doc(src1);

// "Employee" appears twice as idents; the string "Alice" must NOT count.
chk("occurrences of Employee", N.symbolOccurrences(d,"Employee").length, 2);
chk("occurrences of Alice (inside strings only)", N.symbolOccurrences(d,"Alice").length, 0);
chk("occurrences of role", N.symbolOccurrences(d,"role").length, 6);

chk("identifierAt on Employee", N.identifierAt(d,new vscode.Position(0,3)).name, "Employee");
chk("Employee is top-level", N.isTopLevelSymbol(d,"Employee"), true);
chk("HasRole is top-level", N.isTopLevelSymbol(d,"HasRole"), true);
chk("x (local) is not top-level", N.isTopLevelSymbol(d,"x"), false);

const hl=new N.FelidaeDocumentHighlightProvider();
chk("highlight count", hl.provideDocumentHighlights(d,new vscode.Position(0,3)).length, 2);

const rn=new N.FelidaeRenameProvider();
chk("prepareRename returns range", !!rn.prepareRename(d,new vscode.Position(0,3)), true);
let blocked=false; try{ rn.prepareRename(doc("main() =>\n    system.print(value: 1)"),new vscode.Position(1,6)); }catch(e){ blocked=/builtin/.test(e.message); }
chk("rename of builtin is refused", blocked, true);

console.log(`\n${pass} passed, ${fail} failed`); process.exit(fail?1:0);
