const path = require("path");
const {
  executableName, relativeCandidates, searchBases, discoverInterpreters, resolveInterpreter, parseVersion, supportsStdinRuns, searchedLocations
} = require(path.resolve(__dirname, "..", "out", "interpreter.js"));

let pass = 0, fail = 0;
const check = (name, actual, expected) => {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a === e) { pass++; console.log("  ok  ", name); } else { fail++; console.log("  FAIL", name, "\n     exp", e, "\n     act", a); }
};

// A fake file system: only these files exist.
const world = (...files) => {
  const set = new Set(files.map((file) => path.resolve(file).toLowerCase()));
  return (candidate) => set.has(path.resolve(candidate).toLowerCase());
};
const repo = path.resolve("fake-repo");
const exeDebug = path.join(repo, "build", "debug", "x64", "Debug", "felidae.exe");
const exeRelease = path.join(repo, "build", "release", "x64", "Release", "felidae.exe");
const base = { platform: "win32", arch: "x64", workspaceFolders: [], pathDirectories: [] };

check("the executable name follows the platform", [executableName("win32"), executableName("linux")], ["felidae.exe", "felidae"]);
const winCandidates = relativeCandidates("win32", "x64");
check("a release build is preferred over a debug build", winCandidates.indexOf("build/release/x64/Release/felidae.exe") < winCandidates.indexOf("build/debug/x64/Debug/felidae.exe"), true);
check("the Visual Studio debug layout is a known location", winCandidates.includes("build/debug/x64/Debug/felidae.exe"), true);
check("non-Windows locations have no .exe", relativeCandidates("linux", "x64").every((candidate) => !candidate.endsWith(".exe")), true);
check("macOS adds its architecture folder", relativeCandidates("darwin", "arm64")[0], "build/macos-arm64/release/dist/bin/felidae");

// ---- where it looks
check("workspace folders come first, then the notebook folder and its parents",
  searchBases([path.resolve("ws")], path.join(repo, "tests", "notebooks")).slice(0, 4),
  [path.resolve("ws"), path.join(repo, "tests", "notebooks"), path.join(repo, "tests"), repo]);
check("the walk up stops at the root and never repeats a folder",
  new Set(searchBases([], path.join(repo, "a"))).size, searchBases([], path.join(repo, "a")).length);

// ---- your case: a notebook outside any workspace, interpreter only in the debug build folder
const notebookFolder = path.join(repo, "v2_examples");
const found = resolveInterpreter({ ...base, notebookFolder, isFile: world(exeDebug) });
check("a notebook inside the repository finds the repository's debug build with no workspace open", [found.path, found.source], [exeDebug, "search"]);
const both = resolveInterpreter({ ...base, notebookFolder, isFile: world(exeDebug, exeRelease) });
check("with both builds present the release build wins", both.path, exeRelease);
const lost = resolveInterpreter({ ...base, notebookFolder: path.resolve("elsewhere", "Downloads"), isFile: world(exeDebug) });
check("a notebook elsewhere, with nothing configured, finds nothing", [lost.path, lost.source], ["", "none"]);

// ---- the setting and the environment
const configuredAbsolute = path.resolve("tools", "felidae.exe");
const viaSetting = resolveInterpreter({ ...base, notebookFolder: path.resolve("elsewhere"), configured: configuredAbsolute, isFile: world() });
check("a configured path is used even if it does not exist (a wrong setting must fail loudly)", [viaSetting.path, viaSetting.source], [configuredAbsolute, "setting"]);
check("a configured path wins over a build folder", resolveInterpreter({ ...base, notebookFolder, configured: configuredAbsolute, isFile: world(exeDebug) }).path, configuredAbsolute);
check("a relative setting is resolved against the notebook's folders",
  resolveInterpreter({ ...base, notebookFolder: path.join(repo, "v2_examples"), configured: "build/debug/x64/Debug/felidae.exe", isFile: world(exeDebug) }).path, exeDebug);
const onPath = path.resolve("bin");
check("a bare command in the setting is found on PATH",
  resolveInterpreter({ ...base, configured: "felidae", pathDirectories: [onPath], isFile: world(path.join(onPath, "felidae.exe")) }).path, path.join(onPath, "felidae.exe"));
const viaEnvironment = resolveInterpreter({ ...base, environment: configuredAbsolute, isFile: world() });
check("FELIDAE_PATH is used when nothing is configured", [viaEnvironment.path, viaEnvironment.source], [configuredAbsolute, "environment"]);
check("the setting wins over FELIDAE_PATH", resolveInterpreter({ ...base, configured: "a.exe", environment: "b.exe", isFile: world() }).source, "setting");
check("PATH is the last resort",
  resolveInterpreter({ ...base, pathDirectories: [onPath], isFile: world(path.join(onPath, "felidae.exe")) }).source, "path");

// ---- the picker's list
const listed = discoverInterpreters({ ...base, notebookFolder, pathDirectories: [onPath], isFile: world(exeDebug, exeRelease, path.join(onPath, "felidae.exe")) });
check("it lists three distinct interpreters", listed.length, 3);
check("build folders are listed before PATH", listed.map((choice) => choice.source), ["search", "search", "path"]);
check("a missing interpreter's message can say where it looked", searchedLocations({ ...base, notebookFolder, isFile: world() }).slice(-1)[0], "felidae on PATH");

// ---- what a build reports
check("the version is read from --version output", parseVersion("Felidae Logic Programming Language v0.2.3-beta.1\n"), "0.2.3-beta.1");
check("no version in the output gives undefined", parseVersion("something else"), undefined);
const oldHelp = "Usage:\n  felidae --check-json [--stdin] program.fx\nCommands:\n  --check-json [--stdin] program.fx   Parse without opening RocksDB; emit JSON diagnostics and symbols\n";
const newHelp = oldHelp + "  --stdin                             Read the program text from stdin; program.fx only names it (project, imports)\n";
check("an older build's help does not claim stdin runs", supportsStdinRuns(oldHelp), false);
check("a build that runs from stdin says so in its help", supportsStdinRuns(newHelp), true);

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
