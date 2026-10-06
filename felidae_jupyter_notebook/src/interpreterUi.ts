// Choosing and showing the interpreter. Resolution itself is in interpreter.ts;
// this is the VS Code side: the setting, a picker, and a status bar item that
// says which felidae a notebook will use and whether it can run cells.

import * as childProcess from "child_process";
import * as fs from "fs";
import * as path from "path";
import * as vscode from "vscode";
import {
  InterpreterChoice,
  InterpreterSearch,
  discoverInterpreters,
  parseVersion,
  resolveInterpreter,
  searchedLocations,
  supportsStdinRuns
} from "./interpreter";

interface Probe {
  version?: string;
  runsFromStdin: boolean;
}

export class InterpreterManager {
  private readonly status: vscode.StatusBarItem;
  private readonly probes = new Map<string, Promise<Probe>>();

  constructor(context: vscode.ExtensionContext) {
    this.status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 88);
    this.status.command = "felidae.notebook.selectInterpreter";
    context.subscriptions.push(
      this.status,
      vscode.window.onDidChangeActiveNotebookEditor(() => this.refreshStatus()),
      vscode.workspace.onDidChangeConfiguration((event) => {
        if (event.affectsConfiguration("felidae.interpreterPath")) this.refreshStatus();
      }),
      vscode.commands.registerCommand("felidae.notebook.selectInterpreter", () => this.select())
    );
    this.refreshStatus();
  }

  private searchFor(uri?: vscode.Uri): InterpreterSearch {
    const folder = uri ? vscode.workspace.getWorkspaceFolder(uri) ?? vscode.workspace.workspaceFolders?.[0] : vscode.workspace.workspaceFolders?.[0];
    return {
      platform: process.platform,
      arch: process.arch,
      configured: this.workspaceOverride(folder) ?? vscode.workspace.getConfiguration("felidae", uri).get<string>("interpreterPath", ""),
      environment: process.env.FELIDAE_PATH,
      workspaceFolders: (vscode.workspace.workspaceFolders ?? []).map((entry) => entry.uri.fsPath),
      notebookFolder: uri && uri.scheme === "file" ? path.dirname(uri.fsPath) : undefined,
      pathDirectories: (process.env.PATH ?? "").split(path.delimiter).filter(Boolean)
    };
  }

  // .vscode/felidae.json {"interpreterPath": "..."}, the same override the
  // Felidae extension reads.
  private workspaceOverride(folder: vscode.WorkspaceFolder | undefined): string | undefined {
    if (!folder) return undefined;
    try {
      const parsed = JSON.parse(fs.readFileSync(path.join(folder.uri.fsPath, ".vscode", "felidae.json"), "utf8")) as { interpreterPath?: unknown };
      return typeof parsed.interpreterPath === "string" && parsed.interpreterPath.trim() ? parsed.interpreterPath : undefined;
    } catch {
      return undefined;
    }
  }

  resolve(uri?: vscode.Uri): InterpreterChoice {
    return resolveInterpreter(this.searchFor(uri));
  }

  // The message shown when there is no usable interpreter.
  describeMissing(uri: vscode.Uri | undefined, choice: InterpreterChoice): string {
    if (choice.source === "setting" || choice.source === "environment") {
      return "The felidae interpreter " + (choice.source === "setting" ? "set in felidae.interpreterPath" : "named by FELIDAE_PATH") +
        " does not exist: " + choice.path + ". Run 'Felidae: Select Interpreter' to choose another.";
    }
    return "No felidae interpreter was found. Looked in: " + searchedLocations(this.searchFor(uri)).join("; ") +
      ". Run 'Felidae: Select Interpreter' to choose one.";
  }

  // `felidae --version` and `--help`, once per file version. --help tells
  // whether this build can run a program from stdin, which cells need.
  private probe(interpreter: string): Promise<Probe> {
    let key = interpreter;
    try {
      key += "@" + fs.statSync(interpreter).mtimeMs;
    } catch {
      // Missing file: the probe below reports it.
    }
    let probe = this.probes.get(key);
    if (!probe) {
      const run = (argument: string) =>
        new Promise<string>((resolve) => {
          childProcess.execFile(interpreter, [argument], { timeout: 5000, windowsHide: true }, (_error, stdout) => resolve(String(stdout ?? "")));
        });
      probe = Promise.all([run("--version"), run("--help")]).then(([version, help]) => ({
        version: parseVersion(version),
        runsFromStdin: supportsStdinRuns(help)
      }));
      this.probes.set(key, probe);
    }
    return probe;
  }

  refreshStatus(): void {
    const notebook = vscode.window.activeNotebookEditor?.notebook;
    if (!notebook || notebook.notebookType !== "felidae-notebook") {
      this.status.hide();
      return;
    }
    const choice = this.resolve(notebook.uri);
    if (!choice.path || !fs.existsSync(choice.path)) {
      this.status.text = "$(warning) felidae: not found";
      this.status.tooltip = this.describeMissing(notebook.uri, choice);
      this.status.show();
      return;
    }
    this.status.text = "$(terminal) felidae";
    this.status.tooltip = choice.path + " (" + choice.source + "). Click to choose another interpreter.";
    this.status.show();
    void this.probe(choice.path).then((probe) => {
      // The notebook or setting may have changed while the probe ran.
      if (this.resolve(vscode.window.activeNotebookEditor?.notebook.uri).path !== choice.path) return;
      this.status.text = (probe.runsFromStdin ? "$(terminal) felidae " : "$(warning) felidae ") + (probe.version ?? "");
      this.status.tooltip = choice.path + " (" + choice.source + ")" +
        (probe.runsFromStdin ? "" : "\nThis build cannot run notebook cells: it predates `--stdin` for runs. Rebuild felidae or choose another interpreter.") +
        "\nClick to choose another interpreter.";
    });
  }

  async select(): Promise<void> {
    const uri = vscode.window.activeNotebookEditor?.notebook.uri;
    const search = this.searchFor(uri);
    const current = resolveInterpreter(search);
    const found = discoverInterpreters({ ...search, configured: undefined, environment: undefined });

    type Item = vscode.QuickPickItem & { action: "use" | "browse" | "auto"; path?: string };
    const items: Item[] = found.map((choice) => ({
      label: "$(terminal) " + choice.path,
      description: (choice.source === "path" ? "on PATH" : "build folder") + (choice.path === current.path ? " · in use" : ""),
      action: "use",
      path: choice.path
    }));
    items.push(
      { label: "", kind: vscode.QuickPickItemKind.Separator, action: "browse" },
      { label: "$(folder-opened) Browse for felidae…", action: "browse" },
      { label: "$(sync) Detect automatically", description: "clear felidae.interpreterPath", action: "auto" }
    );
    const picked = await vscode.window.showQuickPick(items, {
      title: "Select the felidae interpreter",
      placeHolder: found.length === 0 ? "None found in the usual build folders or on PATH" : "Currently: " + (current.path || "none")
    });
    if (!picked) return;

    let value: string | undefined;
    if (picked.action === "browse") {
      const chosen = await vscode.window.showOpenDialog({
        canSelectFiles: true,
        canSelectFolders: false,
        canSelectMany: false,
        openLabel: "Use this interpreter",
        filters: process.platform === "win32" ? { Executable: ["exe"] } : undefined
      });
      if (!chosen || chosen.length === 0) return;
      value = chosen[0].fsPath;
    } else if (picked.action === "use") {
      value = picked.path;
    }

    // A workspace setting when a folder is open, otherwise a user setting.
    const target = (vscode.workspace.workspaceFolders?.length ?? 0) > 0
      ? vscode.ConfigurationTarget.Workspace
      : vscode.ConfigurationTarget.Global;
    try {
      await vscode.workspace.getConfiguration("felidae").update("interpreterPath", value, target);
    } catch (error) {
      void vscode.window.showErrorMessage("Could not save felidae.interpreterPath: " + (error as Error).message);
      return;
    }
    void vscode.window.showInformationMessage(
      value
        ? "Felidae interpreter set to " + value + " (" + (target === vscode.ConfigurationTarget.Workspace ? "workspace" : "user") + " setting)."
        : "Felidae interpreter will be detected automatically."
    );
    this.refreshStatus();
  }
}
