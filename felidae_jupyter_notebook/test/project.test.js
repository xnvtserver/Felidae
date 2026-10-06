const path = require("path");
const {
  resolveProjectFolder, logicalProgramPath, isMissingInitError, isOutdatedInterpreterError, initFxText, databasePath,
  readDatabaseLocation, summarizeDatabase
} = require(path.resolve(__dirname, "..", "out", "project.js"));

let pass = 0, fail = 0;
const check = (name, actual, expected) => {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a === e) { pass++; console.log("  ok  ", name); } else { fail++; console.log("  FAIL", name, "\n     exp", e, "\n     act", a); }
};

const notebook = path.resolve("projects", "demo", "analysis.fxnb");
const own = path.resolve("projects", "demo");

// ---- which folder is the project
check("by default the notebook's own folder is the project", resolveProjectFolder(notebook, undefined), { folder: own });
check("an empty metadata entry means the same", resolveProjectFolder(notebook, "  "), { folder: own });
check("an absolute project folder is used as it is", resolveProjectFolder(notebook, path.resolve("other", "proj")), { folder: path.resolve("other", "proj") });
check("a relative project folder is relative to the notebook", resolveProjectFolder(notebook, "../shared"), { folder: path.resolve("projects", "shared") });
check("an unsaved notebook with a relative folder explains itself", /not saved yet/.test(resolveProjectFolder(undefined, "../shared").problem), true);
check("an unsaved notebook with an absolute folder works", resolveProjectFolder(undefined, path.resolve("other", "proj")).folder, path.resolve("other", "proj"));
check("an unsaved notebook with no folder says what to do", /Set Notebook Project Folder/.test(resolveProjectFolder(undefined, undefined).problem), true);
check("a non-string metadata value is ignored", resolveProjectFolder(notebook, 42), { folder: own });

check("the logical program name sits in the project folder", logicalProgramPath(own, "analysis.fxnb"), path.join(own, "analysis.fxnb.fx"));

// ---- messages the interpreter really prints
check("a missing init.fx is recognised",
  isMissingInitError("Felidae project requires init.fx beside the entry program: C:\\proj\\init.fx"), true);
check("a missing project directory is recognised", isMissingInitError("Felidae project directory does not exist: C:\\nowhere"), true);
check("another error is not mistaken for it", isMissingInitError("Expected '.' after statement"), false);
check("an interpreter that rejects --stdin for runs is recognised", isOutdatedInterpreterError("--stdin is valid only with --check-json"), true);
check("an interpreter that does not know --stdin is recognised", isOutdatedInterpreterError("Unknown option: --stdin"), true);
check("another error is not mistaken for an old interpreter", isOutdatedInterpreterError("boom"), false);

// ---- the init.fx the extension offers
check("init.fx is the usual two lines, the call ending in a period", initFxText("build/examples/data"), 'import "db".\ndb.location("build/examples/data").\n');
check("backslashes become slashes (a backslash would start an escape)", initFxText("data\\db"), 'import "db".\ndb.location("data/db").\n');
check("a quote in the path is escaped", initFxText('we"ird'), 'import "db".\ndb.location("we\\"ird").\n');
check("surrounding spaces are dropped", initFxText("  ./data.db  "), 'import "db".\ndb.location("./data.db").\n');
check("the message can say where the database ends up", databasePath(own, "./data.db"), path.join(own, "data.db"));
check("a database path is relative to the project folder", databasePath(own, "../shared/data"), path.resolve("projects", "shared", "data"));

// ---- the database, read from init.fx
const usual = 'import "db".\ndb.location("build/examples/data").\n';
check("the database location is read from init.fx", readDatabaseLocation(usual), "build/examples/data");
check("a location without the closing period is still read", readDatabaseLocation('import "db".\ndb.location("a/b")'), "a/b");
check("a commented-out location is ignored", readDatabaseLocation('# db.location("old").\nimport "db".\ndb.location("new").\n'), "new");
check("an init.fx with no location says so", readDatabaseLocation('import "db".\n'), undefined);
check("an escaped quote in the path is unescaped", readDatabaseLocation('db.location("we\\"ird").'), 'we"ird');

const projectFolder = path.resolve("projects", "demo");
check("a missing init.fx is reported with how to fix it", [summarizeDatabase(projectFolder, undefined).state, /Click to create/.test(summarizeDatabase(projectFolder, undefined).tooltip)], ["missing", true]);
check("an init.fx without a location is reported", summarizeDatabase(projectFolder, 'import "db".\n').state, "unconfigured");
const ok = summarizeDatabase(projectFolder, usual);
check("a configured database shows the path as written", [ok.state, ok.text], ["ok", "build/examples/data"]);
check("and the full path in the tooltip", ok.tooltip.includes(path.resolve(projectFolder, "build/examples/data")), true);
check("and which file sets it", ok.tooltip.includes(path.join(projectFolder, "init.fx")), true);
check("what the extension writes reads back the same", readDatabaseLocation(initFxText("./data.db")), "./data.db");

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
