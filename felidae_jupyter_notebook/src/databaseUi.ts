// Shows where the notebook's data goes. A status bar item names the database the
// project's init.fx points at (or says that init.fx is missing or has no
// db.location), with the full path in its tooltip; clicking opens init.fx, or
// offers to create it. It follows the active notebook, the notebook's project
// folder setting, and changes to init.fx itself. The wording is in project.ts.

import * as fs from "fs";
import * as path from "path";
import * as vscode from "vscode";
import { resolveProjectFolder, summarizeDatabase } from "./project";
import { createInitFx, initChanged } from "./projectUi";

export class DatabaseStatus {
  private readonly item: vscode.StatusBarItem;
  private watcher?: vscode.FileSystemWatcher;
  private watchedFolder?: string;

  constructor(context: vscode.ExtensionContext) {
    this.item = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 87);
    this.item.command = "felidae.notebook.openInit";
    context.subscriptions.push(
      this.item,
      { dispose: () => this.watcher?.dispose() },
      vscode.window.onDidChangeActiveNotebookEditor(() => this.refresh()),
      vscode.workspace.onDidChangeNotebookDocument((event) => {
        // Changing the project folder edits the notebook's metadata.
        if (event.notebook.notebookType === "felidae-notebook" && event.metadata) this.refresh();
      }),
      initChanged.event(() => this.refresh()),
      vscode.commands.registerCommand("felidae.notebook.openInit", () => this.openInit())
    );
    this.refresh();
  }

  private activeProject(): { folder?: string; problem?: string } | undefined {
    const notebook = vscode.window.activeNotebookEditor?.notebook;
    if (!notebook || notebook.notebookType !== "felidae-notebook") return undefined;
    return resolveProjectFolder(notebook.uri.scheme === "file" ? notebook.uri.fsPath : undefined, notebook.metadata?.projectFolder);
  }

  private readInit(folder: string): string | undefined {
    try {
      return fs.readFileSync(path.join(folder, "init.fx"), "utf8");
    } catch {
      return undefined;
    }
  }

  refresh(): void {
    const project = this.activeProject();
    if (!project) {
      this.item.hide();
      return;
    }
    if (!project.folder) {
      this.item.text = "$(database) no project folder";
      this.item.tooltip = project.problem;
      this.item.backgroundColor = new vscode.ThemeColor("statusBarItem.warningBackground");
      this.item.show();
      return;
    }
    const summary = summarizeDatabase(project.folder, this.readInit(project.folder));
    this.item.text = "$(database) " + summary.text;
    this.item.tooltip = summary.tooltip;
    // Attention only when something needs doing.
    this.item.backgroundColor = summary.state === "ok" ? undefined : new vscode.ThemeColor("statusBarItem.warningBackground");
    this.item.show();
    this.watch(project.folder);
  }

  // Follow init.fx in the project folder, wherever that is (also outside the workspace).
  private watch(folder: string): void {
    if (this.watchedFolder === folder) return;
    this.watcher?.dispose();
    this.watchedFolder = folder;
    this.watcher = vscode.workspace.createFileSystemWatcher(new vscode.RelativePattern(vscode.Uri.file(folder), "init.fx"));
    const changed = () => {
      this.refresh();
      initChanged.fire();
    };
    this.watcher.onDidCreate(changed);
    this.watcher.onDidChange(changed);
    this.watcher.onDidDelete(changed);
  }

  private async openInit(): Promise<void> {
    const project = this.activeProject();
    if (!project?.folder) {
      void vscode.window.showWarningMessage(project?.problem ?? "Open a Felidae notebook first.");
      return;
    }
    const target = path.join(project.folder, "init.fx");
    if (fs.existsSync(target)) {
      await vscode.window.showTextDocument(vscode.Uri.file(target));
    } else {
      await createInitFx(project.folder);
    }
  }
}
