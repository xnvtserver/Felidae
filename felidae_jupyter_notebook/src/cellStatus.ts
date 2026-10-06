// A line under each code cell saying what it is: "declares total, twice", "query",
// or a warning (a mixed cell, an expression without its period, init.fx lines in
// a cell). It tells you how a cell will be treated before you run it. The wording
// is in program.ts (describeCell).

import * as vscode from "vscode";
import { describeCell } from "./program";

export function registerCellStatus(context: vscode.ExtensionContext): void {
  const changed = new vscode.EventEmitter<void>();
  context.subscriptions.push(
    changed,
    vscode.notebooks.registerNotebookCellStatusBarItemProvider("felidae-notebook", {
      onDidChangeCellStatusBarItems: changed.event,
      provideCellStatusBarItems(cell: vscode.NotebookCell) {
        if (cell.kind !== vscode.NotebookCellKind.Code) return undefined;
        const description = describeCell(cell.document.getText());
        if (!description) return undefined;
        const item = new vscode.NotebookCellStatusBarItem(description.text, vscode.NotebookCellStatusBarAlignment.Left);
        item.tooltip = description.tooltip;
        return item;
      }
    }),
    vscode.workspace.onDidChangeNotebookDocument((event) => {
      if (event.notebook.notebookType !== "felidae-notebook") return;
      if (event.contentChanges.length > 0 || event.cellChanges.some((change) => change.document)) changed.fire();
    })
  );
}
