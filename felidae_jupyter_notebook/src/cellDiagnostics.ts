// Parse-error squiggles in notebook cells: the same red underlines a .fx file
// gets, for code cells. After a pause in typing, the cell's assembled program is
// checked with `felidae --check-json --stdin` (parse only: nothing runs and the
// database is not opened) and errors in this cell are underlined where they are.
// check.ts has the mapping from program positions back to the cell.
//
// Nothing is underlined while the project has no init.fx or there is no
// interpreter; running a cell reports those.
//
// Checks run one at a time (opening a notebook asks for one per cell, and one
// process per cell at once would starve the machine), and a cell whose program
// has not changed since its last check is not checked again.

import * as childProcess from "child_process";
import * as fs from "fs";
import * as path from "path";
import * as vscode from "vscode";
import { CellDiagnostic, cellDiagnostics, planDiagnostics } from "./check";
import { InterpreterManager } from "./interpreterUi";
import { CellInput, planRun } from "./program";
import { logicalProgramPath, resolveProjectFolder } from "./project";
import { initChanged } from "./projectUi";

const SEVERITY: Record<CellDiagnostic["severity"], vscode.DiagnosticSeverity> = {
  error: vscode.DiagnosticSeverity.Error,
  warning: vscode.DiagnosticSeverity.Warning,
  info: vscode.DiagnosticSeverity.Information,
  hint: vscode.DiagnosticSeverity.Hint
};

const CHECK_DELAY_MS = 400;
const CHECK_TIMEOUT_MS = 15000;

export class NotebookCellDiagnostics {
  private readonly collection = vscode.languages.createDiagnosticCollection("felidae-notebook");
  private readonly timers = new Map<string, ReturnType<typeof setTimeout>>();
  private readonly running = new Map<string, childProcess.ChildProcess>();
  private readonly queue = new Map<string, vscode.NotebookCell>();
  // What each cell was last checked against: program, interpreter and index.
  private readonly checked = new Map<string, string>();
  private draining = false;

  constructor(context: vscode.ExtensionContext, private readonly interpreters: InterpreterManager) {
    const isFelidae = (notebook: vscode.NotebookDocument) => notebook.notebookType === "felidae-notebook";
    context.subscriptions.push(
      this.collection,
      vscode.workspace.onDidOpenNotebookDocument((notebook) => isFelidae(notebook) && this.scheduleAll(notebook)),
      vscode.workspace.onDidChangeNotebookDocument((event) => {
        if (!isFelidae(event.notebook)) return;
        // Adding, removing or moving cells changes every cell's program.
        if (event.contentChanges.length > 0) {
          this.scheduleAll(event.notebook);
          return;
        }
        for (const change of event.cellChanges) {
          if (change.document) this.schedule(change.cell);
        }
      }),
      vscode.workspace.onDidCloseNotebookDocument((notebook) => {
        if (!isFelidae(notebook)) return;
        for (const cell of notebook.getCells()) this.forget(cell.document.uri.toString());
      }),
      vscode.workspace.onDidChangeConfiguration((event) => {
        if (!event.affectsConfiguration("felidae.interpreterPath")) return;
        this.checked.clear();
        for (const notebook of vscode.workspace.notebookDocuments) if (isFelidae(notebook)) this.scheduleAll(notebook);
      }),
      // An init.fx appeared or changed: cells could not be checked without one.
      initChanged.event(() => {
        this.checked.clear();
        for (const notebook of vscode.workspace.notebookDocuments) if (isFelidae(notebook)) this.scheduleAll(notebook);
      }),
      { dispose: () => this.disposeAll() }
    );
    for (const notebook of vscode.workspace.notebookDocuments) if (isFelidae(notebook)) this.scheduleAll(notebook);
  }

  private scheduleAll(notebook: vscode.NotebookDocument): void {
    for (const cell of notebook.getCells()) this.schedule(cell);
  }

  private schedule(cell: vscode.NotebookCell): void {
    const key = cell.document.uri.toString();
    const pending = this.timers.get(key);
    if (pending) clearTimeout(pending);
    this.timers.set(key, setTimeout(() => {
      this.timers.delete(key);
      this.queue.set(key, cell);
      void this.drain();
    }, CHECK_DELAY_MS));
  }

  private async drain(): Promise<void> {
    if (this.draining) return;
    this.draining = true;
    try {
      for (const [key, cell] of this.queue) {
        this.queue.delete(key);
        await this.check(cell);
      }
    } finally {
      this.draining = false;
    }
  }

  private forget(key: string): void {
    const pending = this.timers.get(key);
    if (pending) clearTimeout(pending);
    this.timers.delete(key);
    this.queue.delete(key);
    this.checked.delete(key);
    this.running.get(key)?.kill();
    this.running.delete(key);
    this.collection.delete(vscode.Uri.parse(key));
  }

  private disposeAll(): void {
    for (const timer of this.timers.values()) clearTimeout(timer);
    for (const child of this.running.values()) child.kill();
    this.timers.clear();
    this.running.clear();
    this.queue.clear();
  }

  private publish(cell: vscode.NotebookCell, found: CellDiagnostic[]): void {
    const document = cell.document;
    const lastLine = Math.max(0, document.lineCount - 1);
    const clampLine = (line: number) => Math.min(Math.max(0, line), lastLine);
    const clampColumn = (line: number, column: number) => Math.min(Math.max(0, column), document.lineAt(line).text.length);
    this.collection.set(
      document.uri,
      found.map((entry) => {
        const startLine = clampLine(entry.line);
        const endLine = clampLine(entry.endLine);
        const diagnostic = new vscode.Diagnostic(
          new vscode.Range(startLine, clampColumn(startLine, entry.column), endLine, clampColumn(endLine, entry.endColumn)),
          entry.message,
          SEVERITY[entry.severity]
        );
        diagnostic.source = "felidae";
        // The Felidae extension offers "Insert the missing '.'" for this code.
        if (entry.missingPeriodAtEnd) diagnostic.code = "missing-period";
        return diagnostic;
      })
    );
  }

  // Resolves when the check is over (or was not needed), so the queue can move on.
  private check(cell: vscode.NotebookCell): Promise<void> {
    const key = cell.document.uri.toString();
    const notebook = cell.notebook;
    if (cell.kind !== vscode.NotebookCellKind.Code) {
      this.collection.delete(cell.document.uri);
      return Promise.resolve();
    }
    const inputs: CellInput[] = notebook.getCells().map((other) => ({
      kind: other.kind === vscode.NotebookCellKind.Markup ? "markdown" : "code",
      source: other.document.getText()
    }));
    const plan = planRun(inputs, cell.index);
    if (!plan.ok) {
      this.publish(cell, planDiagnostics(plan));
      return Promise.resolve();
    }

    const where = resolveProjectFolder(notebook.uri.scheme === "file" ? notebook.uri.fsPath : undefined, notebook.metadata?.projectFolder);
    const interpreter = this.interpreters.resolve(notebook.uri);
    if (!where.folder || !interpreter.path || !fs.existsSync(interpreter.path)) {
      this.collection.delete(cell.document.uri);
      return Promise.resolve();
    }
    const logicalPath = logicalProgramPath(where.folder, path.basename(notebook.uri.fsPath || notebook.uri.path));

    const stamp = [interpreter.path, logicalPath, cell.index, plan.program].join("\u0000");
    if (this.checked.get(key) === stamp) return Promise.resolve();

    this.running.get(key)?.kill();
    const version = cell.document.version;
    const child = childProcess.spawn(interpreter.path, [logicalPath, "--check-json", "--stdin"], { cwd: where.folder, windowsHide: true });
    this.running.set(key, child);
    let stdout = "";
    const timer = setTimeout(() => child.kill(), CHECK_TIMEOUT_MS);
    child.stdout.on("data", (data: Buffer) => (stdout += data.toString()));
    child.stdin.on("error", () => undefined);
    child.stdin.end(plan.program);
    return new Promise<void>((resolve) => {
      child.on("error", () => {
        clearTimeout(timer);
        if (this.running.get(key) === child) this.running.delete(key);
        resolve();
      });
      child.on("close", () => {
        clearTimeout(timer);
        resolve();
        // A newer check replaced this one, or the cell changed while it ran.
        if (this.running.get(key) !== child) return;
        this.running.delete(key);
        if (cell.document.version !== version) return;
        const found = cellDiagnostics(stdout, plan.lineMap, cell.index, cell.document.getText());
        if (found === undefined) {
          this.collection.delete(cell.document.uri);
          return;
        }
        this.checked.set(key, stamp);
        this.publish(cell, found);
      });
    });
  }
}
