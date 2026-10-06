#!/usr/bin/env node
"use strict";
// Runs every check for the Felidae notebook extension:  npm test
//
// Plain Node harnesses against out/*.js (run `tsc -p ./` first, which
// `npm test` does). The modules under test do not import vscode; what needs a
// real editor host (the controller, the serializer glue, activation) is
// covered by a manual smoke test, as in the Felidae extension.

const path = require("path");
const { execFileSync } = require("child_process");

const HERE = __dirname;
const suites = [
  { name: "notebook file format", file: "format.test.js" },
  { name: "program assembly and line mapping", file: "program.test.js" },
  { name: "parse errors in cells", file: "check.test.js" },
  { name: "value beautifying", file: "beautify.test.js" },
  { name: "interpreter selection", file: "interpreter.test.js" },
  { name: "project folder and init.fx", file: "project.test.js" },
  { name: "cell runner (stdin)", file: "runner.test.js" },
  { name: "serializer (open, edit, save)", file: "serializer.test.js" },
  { name: "controller (running cells)", file: "controller.test.js" },
  { name: "notebook menu", file: "menu.test.js" }
];

let failed = 0;
for (const suite of suites) {
  process.stdout.write(`\n=== ${suite.name} ===\n`);
  try {
    const out = execFileSync(process.execPath, [path.join(HERE, suite.file)], {
      cwd: path.join(HERE, ".."),
      encoding: "utf8"
    });
    console.log(out.trimEnd().split("\n").slice(-1)[0]);
  } catch (error) {
    failed++;
    console.log("FAILED: " + (error.stdout || error.message));
  }
}
console.log(failed === 0 ? "\nall suites passed" : `\n${failed} suite(s) failed`);
process.exit(failed === 0 ? 0 : 1);
