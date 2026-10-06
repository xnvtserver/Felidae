// Runs notebook cells. Each cell is one ordinary felidae process (see
// program.ts and runner.ts); nothing stays alive afterwards and nothing is
// retried, so a failing cell shows the interpreter's own error, mapped back to
// the cell and line it came from.

import * as fs from "fs";
import * as path from "path";
import * as vscode from "vscode";
import { InterpreterChoice } from "./interpreter";
import { CellMetrics, summarizeMetrics } from "./metrics";
import { CellInput, mapProgramErrors, planRun } from "./program";
import { valueOutput } from "./serializer";
import { isMissingInitError, isOutdatedInterpreterError, logicalProgramPath, resolveProjectFolder } from "./project";
import { CellRunner, describeCellRun } from "./runner";

export type LogLevel = "trace" | "debug" | "info" | "warn" | "error";

// What the controller needs from the rest of the extension.
export interface NotebookHost {
  resolveInterpreter(uri: vscode.Uri): InterpreterChoice;
  describeMissingInterpreter(uri: vscode.Uri, choice: InterpreterChoice): string;
  offerSelectInterpreter(): void;
  offerMissingInit(folder: string): void;
  log(level: LogLevel, message: string): void;
}

// The metrics output: time first, then work and database, with hints quoted.
export function metricsMarkdown(metrics: CellMetrics): string {
  const summary = summarizeMetrics(metrics);
  const lines = summary.lines.map((line) => "- " + line);
  const hints = summary.hints.map((hint) => "> ⚠ " + hint);
  return "**Metrics**\n\n" + lines.join("\n") + (hints.length > 0 ? "\n\n" + hints.join("\n\n") : "") + "\n";
}

const errorOutput = (message: string) =>
  new vscode.NotebookCellOutput([vscode.NotebookCellOutputItem.error({ name: "Felidae error", message })]);

export class FelidaeNotebookController {
  private readonly controller: vscode.NotebookController;
  private readonly runner = new CellRunner();
  private order = 0;

  constructor(context: vscode.ExtensionContext, private readonly host: NotebookHost) {
    this.controller = vscode.notebooks.createNotebookController("felidae-notebook-controller", "felidae-notebook", "Felidae");
    this.controller.supportedLanguages = ["felidae"];
    this.controller.supportsExecutionOrder = true;
    this.controller.description = "Each cell runs as one felidae process";
    this.controller.executeHandler = (cells, notebook) => this.execute(cells, notebook);
    this.controller.interruptHandler = () => this.runner.cancel();
    context.subscriptions.push(this.controller, { dispose: () => this.runner.cancel() });
  }

  // Cells run one after another, in the order given (Run All gives file
  // order). The first cell that fails stops the rest.
  private async execute(cells: vscode.NotebookCell[], notebook: vscode.NotebookDocument): Promise<void> {
    for (const cell of cells) {
      if (cell.kind !== vscode.NotebookCellKind.Code) continue;
      if (!(await this.executeCell(cell, notebook))) break;
    }
  }

  private async executeCell(cell: vscode.NotebookCell, notebook: vscode.NotebookDocument): Promise<boolean> {
    const execution = this.controller.createNotebookCellExecution(cell);
    execution.executionOrder = ++this.order;
    execution.start(Date.now());
    await execution.clearOutput();
    const fail = async (message: string): Promise<boolean> => {
      await execution.replaceOutput([errorOutput(message)]);
      execution.end(false, Date.now());
      return false;
    };

    const inputs: CellInput[] = notebook.getCells().map((other) => ({
      kind: other.kind === vscode.NotebookCellKind.Markup ? "markdown" : "code",
      source: other.document.getText()
    }));
    const plan = planRun(inputs, cell.index);
    if (!plan.ok) return fail(plan.message);

    // The project is the folder with init.fx: the notebook's own folder unless
    // its metadata names another (see 'Felidae: Set Notebook Project Folder').
    const notebookFile = notebook.uri.scheme === "file" ? notebook.uri.fsPath : undefined;
    const where = resolveProjectFolder(notebookFile, notebook.metadata?.projectFolder);
    if (!where.folder) return fail(where.problem ?? "No project folder.");
    const folder = where.folder;
    // The assembled program's logical name: it need not exist, it only places
    // init.fx, relative imports and the file name in error messages.
    const logicalPath = logicalProgramPath(folder, path.basename(notebook.uri.fsPath || notebook.uri.path));

    const interpreter = this.host.resolveInterpreter(notebook.uri);
    if (!interpreter.path || !fs.existsSync(interpreter.path)) {
      this.host.offerSelectInterpreter();
      return fail(this.host.describeMissingInterpreter(notebook.uri, interpreter));
    }

    const config = vscode.workspace.getConfiguration("felidae.notebook", notebook.uri);
    const timeoutMs = Math.max(1, config.get<number>("runTimeoutSeconds", 60)) * 1000;
    const args = [logicalPath, "--stdin", "--query", plan.query, "--metrics-json"];
    this.host.log("info", "cell " + (cell.index + 1) + ": " + [interpreter.path, ...args].join(" "));

    const run = await this.runner.run({ command: interpreter.path, args, cwd: folder, timeoutMs, stdin: plan.program });
    const described = describeCellRun(run, timeoutMs);
    if (!described.ok) {
      let message = mapProgramErrors(described.text, plan.lineMap, logicalPath);
      this.host.log("warn", "cell " + (cell.index + 1) + " failed: " + message);
      if (isMissingInitError(described.text)) {
        this.host.offerMissingInit(folder);
        message += "\nCreate an init.fx there (it holds import \"db\". and db.location(\"path\").), or point the notebook at a project folder with 'Felidae: Set Notebook Project Folder'.";
      } else if (isOutdatedInterpreterError(described.text)) {
        message += "\nThis felidae build predates notebook support (it cannot run a program from stdin). Rebuild it, or pick another with 'Felidae: Select Interpreter'.";
      }
      return fail(message);
    }

    const shown = plan.showsValue
      ? described.text || "ok"
      : plan.defined.length > 0 ? "defined: " + plan.defined.join(", ") : "ok";
    const outputs = [valueOutput(shown)];
    if (config.get<boolean>("showMetrics", true) && run.metrics) {
      outputs.push(new vscode.NotebookCellOutput([vscode.NotebookCellOutputItem.text(metricsMarkdown(run.metrics), "text/markdown")]));
    }
    await execution.replaceOutput(outputs);
    execution.end(true, Date.now());
    return true;
  }
}
