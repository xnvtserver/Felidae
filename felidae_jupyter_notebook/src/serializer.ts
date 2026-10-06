// Reads and writes .fxnb files for VS Code's notebook editor, keeping the
// outputs of the last run in the file (format.ts is the file format itself).

import * as vscode from "vscode";
import { beautifyValue, factTable } from "./beautify";
import { StoredCell, StoredOutput, parseNotebook, stringifyNotebook } from "./format";

// The mime type VS Code renders as an error with a name and message.
export const ERROR_MIME = "application/vnd.code.notebook.error";

const decoder = new TextDecoder();
const encoder = new TextEncoder();

// A value, beautified (long lines broken one element per line) and shown
// highlighted as Felidae, with a table above it when the value is rows of facts.
// The plain copy (copy and paste, search, the saved file) is the beautified text.
export function valueOutput(text: string): vscode.NotebookCellOutput {
  const pretty = beautifyValue(text);
  const table = factTable(text);
  const fence = pretty.includes("```") ? "````" : "```";
  const markdown = (table ? table + "\n\n" : "") + fence + "felidae\n" + pretty + "\n" + fence;
  return new vscode.NotebookCellOutput([
    vscode.NotebookCellOutputItem.text(markdown, "text/markdown"),
    vscode.NotebookCellOutputItem.text(pretty, "text/plain")
  ]);
}

export function storedToOutput(stored: StoredOutput): vscode.NotebookCellOutput {
  if (stored.metricsMarkdown !== undefined) {
    return new vscode.NotebookCellOutput([vscode.NotebookCellOutputItem.text(stored.metricsMarkdown, "text/markdown")]);
  }
  if (stored.ok) return valueOutput(stored.text);
  return new vscode.NotebookCellOutput([vscode.NotebookCellOutputItem.error({ name: "Error", message: stored.text })]);
}

function outputToStored(output: vscode.NotebookCellOutput): StoredOutput | undefined {
  const plain = output.items.find((item) => item.mime === "text/plain");
  const error = output.items.find((item) => item.mime === ERROR_MIME);
  const markdown = output.items.find((item) => item.mime === "text/markdown");
  // A value carries a plain copy next to its highlighted one; use the plain one.
  if (plain) return { text: decoder.decode(plain.data), ok: true };
  if (error) {
    const text = decoder.decode(error.data);
    try {
      return { text: (JSON.parse(text) as { message?: string }).message ?? text, ok: false };
    } catch {
      return { text, ok: false };
    }
  }
  // Markdown with no plain copy is the metrics output.
  if (markdown) return { text: "", ok: true, metricsMarkdown: decoder.decode(markdown.data) };
  return undefined;
}

export class FelidaeNotebookSerializer implements vscode.NotebookSerializer {
  // A malformed file throws; VS Code shows the message instead of opening it.
  deserializeNotebook(content: Uint8Array): vscode.NotebookData {
    const stored = parseNotebook(decoder.decode(content));
    const cells = stored.cells.map((cell) => {
      const markdown = cell.kind === "markdown";
      const data = new vscode.NotebookCellData(
        markdown ? vscode.NotebookCellKind.Markup : vscode.NotebookCellKind.Code,
        cell.source,
        markdown ? "markdown" : "felidae"
      );
      if (cell.outputs && cell.outputs.length > 0) data.outputs = cell.outputs.map(storedToOutput);
      if (cell.executionOrder !== undefined) {
        data.executionSummary = {
          executionOrder: cell.executionOrder,
          success: !(cell.outputs ?? []).some((output) => !output.ok)
        };
      }
      return data;
    });
    const notebook = new vscode.NotebookData(cells);
    notebook.metadata = stored.metadata;
    return notebook;
  }

  serializeNotebook(data: vscode.NotebookData): Uint8Array {
    const cells = data.cells.map((cell): StoredCell => {
      const stored: StoredCell = {
        kind: cell.kind === vscode.NotebookCellKind.Markup ? "markdown" : "code",
        source: cell.value
      };
      const outputs = (cell.outputs ?? []).map(outputToStored).filter((output): output is StoredOutput => output !== undefined);
      if (outputs.length > 0) stored.outputs = outputs;
      if (cell.executionSummary?.executionOrder !== undefined) stored.executionOrder = cell.executionSummary.executionOrder;
      return stored;
    });
    return encoder.encode(stringifyNotebook({ version: 1, metadata: (data.metadata ?? {}) as Record<string, unknown>, cells }));
  }
}
