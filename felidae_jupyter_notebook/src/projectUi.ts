// The notebook's project folder and its init.fx, on the VS Code side
// (project.ts holds the rules).
//
// Nothing here creates a file without being asked, and creating init.fx always
// ends with a message saying what was written and where the database is, so a
// notebook's data never appears somewhere unexpected.

import * as fs from "fs";
import * as path from "path";
import * as vscode from "vscode";
import { databasePath, initFxText } from "./project";

export const DEFAULT_DATABASE_LOCATION = "./data.db";

// Fired when an init.fx was created or changed, so the database status and the
// parse checks (which need an init.fx beside the notebook) look again.
export const initChanged = new vscode.EventEmitter<void>();

// Writes init.fx in `folder` after asking where the database should live, then
// tells the user exactly what was created. An existing init.fx is never touched.
export async function createInitFx(folder: string): Promise<boolean> {
  const target = path.join(folder, "init.fx");
  if (fs.existsSync(target)) {
    void vscode.window.showInformationMessage("init.fx already exists in " + folder + "; it was not changed.");
    return true;
  }
  const location = await vscode.window.showInputBox({
    title: "Create init.fx in " + folder,
    prompt: "Where should this project's database be stored? (relative to that folder; written as db.location(...))",
    value: DEFAULT_DATABASE_LOCATION,
    ignoreFocusOut: true,
    validateInput: (value) => (value.trim() === "" ? "A database location is required." : undefined)
  });
  if (location === undefined) return false;

  try {
    fs.writeFileSync(target, initFxText(location), { flag: "wx" });
  } catch (error) {
    void vscode.window.showErrorMessage("Could not create " + target + ": " + (error as Error).message);
    return false;
  }
  initChanged.fire();
  void vscode.window
    .showInformationMessage(
      "Created " + target + ". Facts from this notebook's cells are stored in the database at " + databasePath(folder, location) +
        ' (db.location("' + location.trim().replace(/\\/g, "/") + '")). Edit init.fx to change it.',
      "Open init.fx"
    )
    .then((choice) => {
      if (choice === "Open init.fx") void vscode.window.showTextDocument(vscode.Uri.file(target));
    });
  return true;
}

// Shown when a cell fails because the project folder has no init.fx.
export function offerMissingInit(folder: string): void {
  void vscode.window
    .showWarningMessage(
      "There is no init.fx in " + folder + ", and Felidae needs one to know where the database is.",
      "Create init.fx here",
      "Set Project Folder"
    )
    .then((choice) => {
      if (choice === "Create init.fx here") void createInitFx(folder);
      else if (choice === "Set Project Folder") void vscode.commands.executeCommand("felidae.notebook.setProjectFolder");
    });
}

export function offerSelectInterpreter(): void {
  void vscode.window
    .showErrorMessage("No usable felidae interpreter was found.", "Select Interpreter")
    .then((choice) => {
      if (choice === "Select Interpreter") void vscode.commands.executeCommand("felidae.notebook.selectInterpreter");
    });
}

// 'Felidae: Set Notebook Project Folder': point the open notebook at a folder
// with an init.fx. Stored in the notebook's own metadata, relative to it.
export async function setProjectFolder(): Promise<void> {
  const editor = vscode.window.activeNotebookEditor;
  if (!editor || editor.notebook.notebookType !== "felidae-notebook") {
    void vscode.window.showWarningMessage("Open a Felidae notebook first.");
    return;
  }
  const notebook = editor.notebook;
  const notebookFolder = notebook.uri.scheme === "file" ? path.dirname(notebook.uri.fsPath) : undefined;

  const choice = await vscode.window.showQuickPick(
    [
      { label: "$(folder-opened) Choose a folder…", own: false },
      { label: "$(notebook) Use the notebook's own folder", description: notebookFolder, own: true }
    ],
    { title: "Project folder for this notebook (the folder that holds init.fx)" }
  );
  if (!choice) return;

  let stored: string | undefined;
  if (!choice.own) {
    const picked = await vscode.window.showOpenDialog({
      canSelectFolders: true,
      canSelectFiles: false,
      canSelectMany: false,
      openLabel: "Use as project folder"
    });
    if (!picked || picked.length === 0) return;
    const folder = picked[0].fsPath;
    if (!fs.existsSync(path.join(folder, "init.fx"))) {
      const action = await vscode.window.showWarningMessage(
        folder + " has no init.fx. Felidae needs one there to know where the database is.",
        "Create init.fx there",
        "Use it anyway"
      );
      if (!action) return;
      if (action === "Create init.fx there" && !(await createInitFx(folder))) return;
    }
    // Relative to the notebook when it is saved, so the pair can move together.
    const relative = notebookFolder ? path.relative(notebookFolder, folder) : "";
    stored = notebookFolder && relative !== "" && !path.isAbsolute(relative) ? relative.replace(/\\/g, "/") : folder;
  }

  const metadata: { [key: string]: unknown } = { ...notebook.metadata };
  if (stored === undefined) delete metadata.projectFolder;
  else metadata.projectFolder = stored;
  const edit = new vscode.WorkspaceEdit();
  edit.set(notebook.uri, [vscode.NotebookEdit.updateNotebookMetadata(metadata)]);
  await vscode.workspace.applyEdit(edit);

  void vscode.window.showInformationMessage(
    stored === undefined
      ? "This notebook now uses its own folder as the project. Its init.fx decides the database."
      : "This notebook's project folder is " + stored + ". The init.fx there decides the database. Save the notebook to keep this."
  );
}
