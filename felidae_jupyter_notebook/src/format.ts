// The .fxnb notebook file: a JSON document like .ipynb, with markdown and code
// cells and the outputs of the last run.
//
//   {
//     "version": 1,
//     "metadata": {},
//     "cells": [
//       { "kind": "markdown", "source": ["# Title\n"] },
//       { "kind": "code", "source": ["def total := 40 + 2.\n"],
//         "executionOrder": 1,
//         "outputs": [{ "text": "42", "ok": true }] }
//     ]
//   }
//
// "source" is written as an array of lines (each keeping its newline) so a
// notebook diffs line by line; a plain string is accepted when reading.
// No vscode import, so it is unit-testable.

export const NOTEBOOK_VERSION = 1;

export type CellKind = "code" | "markdown";

export interface StoredOutput {
  text: string;
  // false for an error; shown as an error output.
  ok: boolean;
  // Interpreter metrics as markdown, kept as a second output.
  metricsMarkdown?: string;
}

export interface StoredCell {
  kind: CellKind;
  source: string;
  outputs?: StoredOutput[];
  executionOrder?: number;
}

export interface StoredNotebook {
  version: number;
  metadata: Record<string, unknown>;
  cells: StoredCell[];
}

export class NotebookFormatError extends Error {}

export function emptyNotebook(): StoredNotebook {
  return { version: NOTEBOOK_VERSION, metadata: {}, cells: [] };
}

function sourceText(value: unknown, where: string): string {
  if (typeof value === "string") return value;
  if (Array.isArray(value) && value.every((line) => typeof line === "string")) return value.join("");
  throw new NotebookFormatError(where + ': "source" must be a string or an array of strings');
}

// A zero-byte file is a new, empty notebook (what VS Code creates for a fresh
// file). Anything else must be a valid notebook: it is never repaired or
// guessed at, the error says what is wrong.
export function parseNotebook(text: string): StoredNotebook {
  if (text.trim() === "") return emptyNotebook();

  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch (error) {
    throw new NotebookFormatError("Not a valid .fxnb file (invalid JSON): " + (error as Error).message);
  }
  if (typeof raw !== "object" || raw === null || Array.isArray(raw)) {
    throw new NotebookFormatError("Not a valid .fxnb file: the top level must be an object.");
  }
  const document = raw as Record<string, unknown>;
  if (document.version !== NOTEBOOK_VERSION) {
    throw new NotebookFormatError("Unsupported .fxnb version " + JSON.stringify(document.version) + "; this extension reads version " + NOTEBOOK_VERSION + ".");
  }
  if (!Array.isArray(document.cells)) throw new NotebookFormatError('Not a valid .fxnb file: "cells" must be an array.');

  const cells = document.cells.map((entry, index): StoredCell => {
    const where = "cell " + (index + 1);
    if (typeof entry !== "object" || entry === null) throw new NotebookFormatError(where + " must be an object.");
    const cell = entry as Record<string, unknown>;
    if (cell.kind !== "code" && cell.kind !== "markdown") {
      throw new NotebookFormatError(where + ': "kind" must be "code" or "markdown".');
    }
    const stored: StoredCell = { kind: cell.kind, source: sourceText(cell.source ?? "", where) };
    if (cell.executionOrder !== undefined) {
      if (typeof cell.executionOrder !== "number") throw new NotebookFormatError(where + ': "executionOrder" must be a number.');
      stored.executionOrder = cell.executionOrder;
    }
    if (cell.outputs !== undefined) {
      if (!Array.isArray(cell.outputs)) throw new NotebookFormatError(where + ': "outputs" must be an array.');
      stored.outputs = cell.outputs.map((output, outputIndex): StoredOutput => {
        const item = output as Record<string, unknown>;
        if (typeof item !== "object" || item === null || typeof item.text !== "string" || typeof item.ok !== "boolean") {
          throw new NotebookFormatError(where + ", output " + (outputIndex + 1) + ': needs a string "text" and a boolean "ok".');
        }
        const result: StoredOutput = { text: item.text, ok: item.ok };
        if (typeof item.metricsMarkdown === "string") result.metricsMarkdown = item.metricsMarkdown;
        return result;
      });
    }
    return stored;
  });

  const metadata = typeof document.metadata === "object" && document.metadata !== null && !Array.isArray(document.metadata)
    ? (document.metadata as Record<string, unknown>)
    : {};
  return { version: NOTEBOOK_VERSION, metadata, cells };
}

// Lines keep their newline so that joining them gives the source back exactly.
function splitSource(source: string): string[] {
  if (source === "") return [];
  return source.match(/[^\n]*\n|[^\n]+$/g) ?? [source];
}

export function stringifyNotebook(notebook: StoredNotebook): string {
  const document = {
    version: NOTEBOOK_VERSION,
    metadata: notebook.metadata,
    cells: notebook.cells.map((cell) => {
      const entry: Record<string, unknown> = { kind: cell.kind, source: splitSource(cell.source) };
      if (cell.executionOrder !== undefined) entry.executionOrder = cell.executionOrder;
      if (cell.outputs && cell.outputs.length > 0) entry.outputs = cell.outputs;
      return entry;
    })
  };
  return JSON.stringify(document, null, 2) + "\n";
}
