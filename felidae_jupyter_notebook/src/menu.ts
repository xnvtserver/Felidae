// The Felidae notebook menu: one place to reach everything a notebook offers,
// from the notebook toolbar or "Felidae: Notebook Menu". Pure data, so the test
// can check that every command in it exists.

export interface MenuEntry {
  label: string;
  detail: string;
  command: string;
}

export const NOTEBOOK_MENU: readonly MenuEntry[] = [
  { label: "$(run-all) Run all cells", detail: "In order; stops at the first cell that fails", command: "notebook.execute" },
  { label: "$(clear-all) Clear all outputs", detail: "Outputs are saved in the .fxnb file; this removes them", command: "notebook.clearAllCellsOutputs" },
  { label: "$(terminal) Open REPL", detail: "felidae --repl in the notebook's project folder", command: "felidae.notebook.openRepl" },
  { label: "$(debug-console) Send selected cell to REPL", detail: "Declarations from other cells are not loaded for you", command: "felidae.notebook.sendToRepl" },
  { label: "$(database) Open init.fx", detail: "The project's manifest: database location and imports", command: "felidae.notebook.openInit" },
  { label: "$(folder) Set project folder", detail: "Where init.fx and the database live", command: "felidae.notebook.setProjectFolder" },
  { label: "$(settings-gear) Select interpreter", detail: "Choose the felidae executable", command: "felidae.notebook.selectInterpreter" },
  { label: "$(export) Export as .fx", detail: "The notebook's code as one Felidae source file", command: "felidae.notebook.exportFx" },
  { label: "$(output) Show output", detail: "The extension's log", command: "felidae.notebook.showOutput" }
];
