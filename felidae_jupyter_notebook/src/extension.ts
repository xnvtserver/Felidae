// Felidae Notebook: notebooks (.fxnb) for the Felidae DSL. Cells run through a
// felidae interpreter this extension finds itself (setting, environment, build
// folders, PATH; or pick one with 'Felidae: Select Interpreter'). Highlighting
// and language features come from the Felidae extension, which it depends on.

import * as path from "path";
import * as vscode from "vscode";
import { FelidaeNotebookController, LogLevel } from "./controller";
import { NotebookCellDiagnostics } from "./cellDiagnostics";
import { registerCellStatus } from "./cellStatus";
import { DatabaseStatus } from "./databaseUi";
import { InterpreterManager } from "./interpreterUi";
import { CellInput, exportFx } from "./program";
import { NOTEBOOK_MENU } from "./menu";
import { registerNotebookRepl } from "./repl";
import { createInitFx, offerMissingInit, offerSelectInterpreter, setProjectFolder } from "./projectUi";
import { FelidaeNotebookSerializer } from "./serializer";
import { resolveProjectFolder } from "./project";

// A new notebook starts with a short guide. The fenced blocks are highlighted
// as Felidae because the Felidae extension registers that language.
const NEW_NOTEBOOK_INTRO = [
  "# Felidae notebook",
  "",
  "**Declarations** build the program; a cell with **one expression** ending in `.` is a query:",
  "",
  "```felidae",
  "def total := 40 + 2.",
  "```",
  "",
  "```felidae",
  "total + 1.",
  "```",
  "",
  "Each cell runs as its own `felidae` process. This folder is the project: it needs an `init.fx`",
  "(the manifest that says where the database is), for example:",
  "",
  "```felidae",
  'import "db".',
  'db.location("./data.db").',
  "```",
  "",
  "If it is missing, running a cell offers to create one. *Felidae: Set Notebook Project Folder* uses another folder's."
].join("\n");

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  const channel = vscode.window.createOutputChannel("Felidae Notebook", { log: true });
  context.subscriptions.push(channel);
  const log = (level: LogLevel, message: string): void => channel[level](message);

  const interpreters = new InterpreterManager(context);
  new NotebookCellDiagnostics(context, interpreters);
  new DatabaseStatus(context);
  registerNotebookRepl(context, interpreters, (level, message) => log(level, message));
  registerCellStatus(context);

  context.subscriptions.push(
    vscode.workspace.registerNotebookSerializer("felidae-notebook", new FelidaeNotebookSerializer(), {
      transientOutputs: false
    })
  );

  new FelidaeNotebookController(context, {
    resolveInterpreter: (uri) => interpreters.resolve(uri),
    describeMissingInterpreter: (uri, choice) => interpreters.describeMissing(uri, choice),
    offerSelectInterpreter,
    offerMissingInit,
    log
  });

  context.subscriptions.push(
    vscode.commands.registerCommand("felidae.notebook.showOutput", () => channel.show(true)),
    vscode.commands.registerCommand("felidae.notebook.menu", async () => {
      const picked = await vscode.window.showQuickPick([...NOTEBOOK_MENU], { title: "Felidae Notebook", placeHolder: "What do you want to do?" });
      if (picked) await vscode.commands.executeCommand(picked.command);
    }),
    vscode.commands.registerCommand("felidae.notebook.setProjectFolder", () => setProjectFolder()),
    vscode.commands.registerCommand("felidae.notebook.createInit", async () => {
      const notebook = vscode.window.activeNotebookEditor?.notebook;
      if (!notebook || notebook.notebookType !== "felidae-notebook") {
        void vscode.window.showWarningMessage("Open a Felidae notebook first.");
        return;
      }
      const where = resolveProjectFolder(notebook.uri.scheme === "file" ? notebook.uri.fsPath : undefined, notebook.metadata?.projectFolder);
      if (!where.folder) {
        void vscode.window.showWarningMessage(where.problem ?? "No project folder.");
        return;
      }
      await createInitFx(where.folder);
    }),
    vscode.commands.registerCommand("felidae.notebook.new", async () => {
      const data = new vscode.NotebookData([
        new vscode.NotebookCellData(vscode.NotebookCellKind.Markup, NEW_NOTEBOOK_INTRO, "markdown"),
        new vscode.NotebookCellData(vscode.NotebookCellKind.Code, "def total := 40 + 2.", "felidae"),
        new vscode.NotebookCellData(vscode.NotebookCellKind.Code, "total + 1.", "felidae")
      ]);
      const notebook = await vscode.workspace.openNotebookDocument("felidae-notebook", data);
      await vscode.window.showNotebookDocument(notebook);
    }),
    vscode.commands.registerCommand("felidae.notebook.exportFx", async () => {
      const editor = vscode.window.activeNotebookEditor;
      if (!editor || editor.notebook.notebookType !== "felidae-notebook") {
        void vscode.window.showWarningMessage("Open a Felidae notebook to export it.");
        return;
      }
      const cells: CellInput[] = editor.notebook.getCells().map((cell) => ({
        kind: cell.kind === vscode.NotebookCellKind.Markup ? "markdown" : "code",
        source: cell.document.getText()
      }));
      const suggested = editor.notebook.uri.scheme === "file"
        ? vscode.Uri.file(editor.notebook.uri.fsPath.replace(/\.fxnb$/i, "") + ".fx")
        : undefined;
      const target = await vscode.window.showSaveDialog({ defaultUri: suggested, filters: { "Felidae source": ["fx"] } });
      if (!target) return;
      await vscode.workspace.fs.writeFile(target, new TextEncoder().encode(exportFx(cells)));
      log("info", "exported " + path.basename(editor.notebook.uri.fsPath) + " to " + target.fsPath);
    })
  );
}

export function deactivate(): void {
  // Cell processes end with their cells; nothing is left running.
}
