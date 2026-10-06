// The interpreter's own REPL for a notebook, to try a cell's code interactively.
//
//   Felidae: Open Notebook REPL      starts `felidae --repl` in a terminal, in the
//                                    notebook's project folder (the REPL reads ./init.fx)
//   Felidae: Send Cell to REPL       sends the selected code cell(s)
//
// The REPL is the interpreter's normal interactive mode in a terminal you own:
// the extension keeps no session of its own. It holds the project's database
// open, so running a notebook cell while it is open reports RocksDB's lock error.
// Declarations from other cells are not loaded for you: send them too, in order.

import * as fs from "fs";
import * as path from "path";
import * as vscode from "vscode";
import { InterpreterManager } from "./interpreterUi";
import { resolveProjectFolder } from "./project";
import { offerMissingInit, offerSelectInterpreter } from "./projectUi";

export function registerNotebookRepl(context: vscode.ExtensionContext, interpreters: InterpreterManager, log: (level: "info", message: string) => void): void {
  let terminal: vscode.Terminal | undefined;
  context.subscriptions.push(
    vscode.window.onDidCloseTerminal((closed) => {
      if (closed === terminal) terminal = undefined;
    })
  );

  const activeNotebook = (): vscode.NotebookEditor | undefined => {
    const editor = vscode.window.activeNotebookEditor;
    if (editor && editor.notebook.notebookType === "felidae-notebook") return editor;
    void vscode.window.showWarningMessage("Open a Felidae notebook first.");
    return undefined;
  };

  const open = (editor: vscode.NotebookEditor): vscode.Terminal | undefined => {
    if (terminal && terminal.exitStatus === undefined) {
      terminal.show(true);
      return terminal;
    }
    const notebook = editor.notebook;
    const where = resolveProjectFolder(notebook.uri.scheme === "file" ? notebook.uri.fsPath : undefined, notebook.metadata?.projectFolder);
    if (!where.folder) {
      void vscode.window.showWarningMessage(where.problem ?? "No project folder.");
      return undefined;
    }
    if (!fs.existsSync(path.join(where.folder, "init.fx"))) {
      offerMissingInit(where.folder);
      return undefined;
    }
    const interpreter = interpreters.resolve(notebook.uri);
    if (!interpreter.path || !fs.existsSync(interpreter.path)) {
      offerSelectInterpreter();
      return undefined;
    }
    log("info", "repl: " + interpreter.path + " --repl (cwd " + where.folder + ")");
    // The terminal runs felidae itself, not a shell, so it ends when the REPL does.
    terminal = vscode.window.createTerminal({ name: "Felidae Notebook REPL", cwd: where.folder, shellPath: interpreter.path, shellArgs: ["--repl"] });
    terminal.show(true);
    return terminal;
  };

  context.subscriptions.push(
    vscode.commands.registerCommand("felidae.notebook.openRepl", () => {
      const editor = activeNotebook();
      if (editor) open(editor);
    }),
    vscode.commands.registerCommand("felidae.notebook.sendToRepl", () => {
      const editor = activeNotebook();
      if (!editor) return;
      // The cells in the selection, or the first selected cell, in notebook order.
      const wanted = new Set<number>();
      for (const range of editor.selections) for (let index = range.start; index < range.end; index++) wanted.add(index);
      const cells = editor.notebook
        .getCells()
        .filter((cell) => wanted.has(cell.index) && cell.kind === vscode.NotebookCellKind.Code)
        .map((cell) => cell.document.getText().replace(/\s+$/, ""))
        .filter((text) => text !== "");
      if (cells.length === 0) {
        void vscode.window.showInformationMessage("Select a code cell to send to the REPL.");
        return;
      }
      const target = open(editor);
      if (!target) return;
      for (const text of cells) target.sendText(text, true);
    })
  );
}
