"use strict";
const path = require("path");
const { NOTEBOOK_MENU } = require(path.resolve(__dirname, "..", "out", "menu.js"));
const pkg = require(path.resolve(__dirname, "..", "package.json"));

let pass = 0, fail = 0;
const check = (name, actual, expected) => {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  if (a === e) { pass++; console.log("  ok  ", name); } else { fail++; console.log("  FAIL", name, "\n     exp", e, "\n     act", a); }
};

// Commands VS Code itself provides for notebooks.
const BUILT_IN = new Set(["notebook.execute", "notebook.clearAllCellsOutputs"]);
const contributed = new Set(pkg.contributes.commands.map((command) => command.command));

check("every menu command is contributed by this extension or built into VS Code",
  NOTEBOOK_MENU.filter((entry) => !contributed.has(entry.command) && !BUILT_IN.has(entry.command)).map((entry) => entry.command), []);
check("every contributed command except the menu itself is reachable from the menu",
  [...contributed].filter((command) => command !== "felidae.notebook.menu" && command !== "felidae.notebook.new" && command !== "felidae.notebook.createInit"
    && !NOTEBOOK_MENU.some((entry) => entry.command === command)), []);
check("no command appears twice", new Set(NOTEBOOK_MENU.map((entry) => entry.command)).size, NOTEBOOK_MENU.length);
check("every entry has a label and a detail", NOTEBOOK_MENU.every((entry) => entry.label && entry.detail), true);

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
