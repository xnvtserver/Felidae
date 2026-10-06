// The notebook's project: the folder that holds init.fx, which the interpreter
// requires beside the entry program (it does not search parent folders). The
// database and relative imports both come from there.
//
// By default that is the notebook's own folder. A notebook can point at another
// folder with the "projectFolder" entry in its metadata, so it can live anywhere
// (a Downloads folder, say) and still use a project's database.
// No vscode import, so it is unit-testable.

import * as path from "path";

export interface ProjectFolder {
  folder?: string;
  // Why there is no folder.
  problem?: string;
}

// `notebookFile` is the notebook's path when it is saved to disk; `metadataFolder`
// is the value of metadata.projectFolder, if any, absolute or relative to the notebook.
export function resolveProjectFolder(notebookFile: string | undefined, metadataFolder: unknown): ProjectFolder {
  if (typeof metadataFolder === "string" && metadataFolder.trim() !== "") {
    const wanted = metadataFolder.trim();
    if (path.isAbsolute(wanted)) return { folder: path.normalize(wanted) };
    if (!notebookFolderOf(notebookFile)) {
      return { problem: "The project folder is relative (" + wanted + ") but the notebook is not saved yet. Save it, or choose an absolute folder." };
    }
    return { folder: path.resolve(notebookFolderOf(notebookFile) as string, wanted) };
  }
  const own = notebookFolderOf(notebookFile);
  if (own) return { folder: own };
  return {
    problem: "This notebook is not saved in a folder yet. Save it next to an init.fx, or run 'Felidae: Set Notebook Project Folder' to use a project folder."
  };
}

function notebookFolderOf(notebookFile: string | undefined): string | undefined {
  return notebookFile && path.isAbsolute(notebookFile) ? path.dirname(notebookFile) : undefined;
}

// The program name given to felidae: it need not exist, it only places the
// project (init.fx), relative imports and the file name in error messages.
export function logicalProgramPath(folder: string, notebookName: string): string {
  return path.join(folder, notebookName + ".fx");
}

// What a missing init.fx looks like when the interpreter reports it.
export function isMissingInitError(message: string): boolean {
  return /requires init\.fx beside the entry program|project directory does not exist/i.test(message);
}

// An interpreter that predates `--stdin` for runs rejects it with one of these.
export function isOutdatedInterpreterError(message: string): boolean {
  return /--stdin is valid only with --check-json|Unknown option: --stdin/i.test(message);
}

// The init.fx the extension offers to create. Slashes are normalised because a
// backslash would start an escape inside a Felidae string.
export function initFxText(databaseLocation: string): string {
  const location = databaseLocation.trim().replace(/\\/g, "/").replace(/"/g, '\\"');
  return 'import "db".\ndb.location("' + location + '").\n';
}

// The path in the `db.location("...")` call of an init.fx text, if it has one.
export function readDatabaseLocation(initText: string): string | undefined {
  const withoutComments = initText.replace(/^\s*#.*$/gm, "");
  const match = /^\s*db\.location\(\s*"((?:[^"\\]|\\.)*)"\s*\)/m.exec(withoutComments);
  return match ? match[1].replace(/\\(.)/g, "$1") : undefined;
}

export interface DatabaseSummary {
  state: "missing" | "unconfigured" | "ok";
  // Short, for the status bar.
  text: string;
  tooltip: string;
}

// What to tell the user about the project's database. `initText` is the content
// of init.fx, or undefined when the file does not exist.
export function summarizeDatabase(folder: string, initText: string | undefined): DatabaseSummary {
  if (initText === undefined) {
    return {
      state: "missing",
      text: "no init.fx",
      tooltip: "There is no init.fx in " + folder + ", so Felidae does not know where the database is. Click to create one."
    };
  }
  const location = readDatabaseLocation(initText);
  if (location === undefined) {
    return {
      state: "unconfigured",
      text: "db: not set",
      tooltip: "init.fx in " + folder + ' has no db.location("..."). Click to open it.'
    };
  }
  const resolved = databasePath(folder, location);
  return {
    state: "ok",
    text: location,
    tooltip: "Database: " + resolved + "\nSet by db.location(\"" + location + "\") in " + path.join(folder, "init.fx") + ". Click to open init.fx."
  };
}

// Where that database ends up, to tell the user.
export function databasePath(folder: string, databaseLocation: string): string {
  return path.resolve(folder, databaseLocation.trim());
}
