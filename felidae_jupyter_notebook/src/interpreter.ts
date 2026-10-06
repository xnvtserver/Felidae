// Finding the felidae interpreter, and listing the ones there are to choose from.
//
// Resolution order, first match wins:
//   1. the felidae.interpreterPath setting (also read from .vscode/felidae.json)
//   2. the FELIDAE_PATH environment variable
//   3. the first interpreter found at a known build or release location, looked
//      for under every workspace folder and under the notebook's own folder and
//      its parents (so a notebook inside the repository finds the repository's
//      build, with or without an open workspace)
//   4. felidae on PATH
//
// A configured path is used as it is, even when it does not exist: a wrong
// setting should fail loudly rather than be silently replaced by a search.
// No vscode import, so it is unit-testable.

import * as fs from "fs";
import * as path from "path";

export type InterpreterSource = "setting" | "environment" | "search" | "path" | "none";

export interface InterpreterChoice {
  path: string;
  source: InterpreterSource;
}

export interface InterpreterSearch {
  platform: NodeJS.Platform;
  arch: string;
  // felidae.interpreterPath (or the workspace's felidae.json), if set.
  configured?: string;
  // FELIDAE_PATH, if set.
  environment?: string;
  workspaceFolders: string[];
  // The folder holding the notebook file, if it has one.
  notebookFolder?: string;
  pathDirectories: string[];
  isFile?: (candidate: string) => boolean;
}

const defaultIsFile = (candidate: string): boolean => {
  try {
    return fs.statSync(candidate).isFile();
  } catch {
    return false;
  }
};

export function executableName(platform: NodeJS.Platform): string {
  return platform === "win32" ? "felidae.exe" : "felidae";
}

// Where builds put the interpreter, relative to a repository root. Release
// before debug: a release build is the one meant to be used.
export function relativeCandidates(platform: NodeJS.Platform, arch: string): string[] {
  const exe = executableName(platform);
  if (platform === "win32") {
    return [
      `build/release/x64/Release/${exe}`,
      `build/debug/x64/Debug/${exe}`,
      `build/windows-x64/release/dist/bin/${exe}`,
      `build/release/dist/bin/${exe}`,
      `dist/bin/${exe}`,
      `release/bin/${exe}`
    ];
  }
  const macos = platform === "darwin" ? [`build/macos-${arch === "arm64" ? "arm64" : "x86_64"}/release/dist/bin/${exe}`] : [];
  return [
    ...macos,
    `build/release/dist/bin/${exe}`,
    `build/release/${exe}`,
    `build/debug/${exe}`,
    `dist/bin/${exe}`,
    `release/bin/${exe}`
  ];
}

// The workspace folders, then the notebook's folder and its parents, nearest first.
export function searchBases(workspaceFolders: string[], notebookFolder?: string): string[] {
  const bases: string[] = [...workspaceFolders];
  if (notebookFolder) {
    let folder = path.resolve(notebookFolder);
    for (let depth = 0; depth < 8; depth++) {
      bases.push(folder);
      const parent = path.dirname(folder);
      if (parent === folder) break;
      folder = parent;
    }
  }
  return [...new Set(bases)];
}

// Every interpreter that exists at a known location or on PATH, in the order
// resolution would prefer them. Used by the picker.
export function discoverInterpreters(search: InterpreterSearch): InterpreterChoice[] {
  const isFile = search.isFile ?? defaultIsFile;
  const found: InterpreterChoice[] = [];
  const seen = new Set<string>();
  const add = (candidate: string, source: InterpreterSource) => {
    const key = path.resolve(candidate).toLowerCase();
    if (seen.has(key) || !isFile(candidate)) return;
    seen.add(key);
    found.push({ path: path.resolve(candidate), source });
  };
  for (const base of searchBases(search.workspaceFolders, search.notebookFolder)) {
    for (const relative of relativeCandidates(search.platform, search.arch)) add(path.join(base, relative), "search");
  }
  for (const directory of search.pathDirectories) add(path.join(directory, executableName(search.platform)), "path");
  return found;
}

// A configured value may be absolute, relative to a workspace or notebook
// folder, or a bare command found on PATH. Anything else is used as written.
function resolveConfigured(value: string, search: InterpreterSearch): string {
  const isFile = search.isFile ?? defaultIsFile;
  const withSuffix = (candidate: string) =>
    search.platform === "win32" && !path.extname(candidate) && !isFile(candidate) ? candidate + ".exe" : candidate;
  const trimmed = value.trim();
  if (path.isAbsolute(trimmed)) return withSuffix(trimmed);
  for (const base of searchBases(search.workspaceFolders, search.notebookFolder)) {
    const local = withSuffix(path.join(base, trimmed));
    if (isFile(local)) return local;
  }
  if (!/[/\\]/.test(trimmed)) {
    for (const directory of search.pathDirectories) {
      const onPath = withSuffix(path.join(directory, trimmed));
      if (isFile(onPath)) return onPath;
    }
  }
  return withSuffix(path.resolve(search.workspaceFolders[0] ?? search.notebookFolder ?? ".", trimmed));
}

export function resolveInterpreter(search: InterpreterSearch): InterpreterChoice {
  if (search.configured?.trim()) return { path: resolveConfigured(search.configured, search), source: "setting" };
  if (search.environment?.trim()) return { path: resolveConfigured(search.environment, search), source: "environment" };
  const found = discoverInterpreters(search)[0];
  return found ?? { path: "", source: "none" };
}

// Where a missing interpreter was looked for, to tell the user.
export function searchedLocations(search: InterpreterSearch): string[] {
  const relative = relativeCandidates(search.platform, search.arch);
  const bases = searchBases(search.workspaceFolders, search.notebookFolder);
  return [
    ...bases.slice(0, 3).map((base) => path.join(base, relative[0]) + "  (and the other known build folders)"),
    "felidae on PATH"
  ];
}

// `felidae --version` prints e.g. "Felidae Logic Programming Language v0.2.3-beta.1".
export function parseVersion(output: string): string | undefined {
  return /\bv(\d[^\s]*)/.exec(output)?.[1];
}

// Only an interpreter that can run a program from stdin (felidae file.fx
// --stdin) can run notebook cells. Its --help says so; older builds do not.
export function supportsStdinRuns(helpText: string): boolean {
  return /program text from stdin/i.test(helpText);
}
