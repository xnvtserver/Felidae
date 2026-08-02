#!/usr/bin/env node
// Copies the checked-in ranking models from ml/models into resources/models so
// they get bundled into the packaged extension. Same "single source, generated
// copy" pattern as generate-builtin-docs.js and generate-libraries.js.
//
// The models are produced by `cd ml && npm install && npm run train`; nothing
// here trains anything, and no ML runtime ships with the extension - only the
// small tree evaluator in src/mlRanking.ts.

"use strict";

const fs = require("fs");
const path = require("path");

const MODEL_FILES = ["completion-rank.json", "next-param.json", "corpus-index.json"];

function main() {
  const repoRoot = path.resolve(__dirname, "..", "..");
  const sourceDir = path.join(repoRoot, "ml", "models");
  const destDir = path.join(__dirname, "..", "resources", "models");

  if (!fs.existsSync(sourceDir)) {
    console.error(
      `generate-models: ${sourceDir} not found. Ranking stays disabled; ` +
        "run `cd ml && npm install && npm run train` to produce it."
    );
    process.exit(0);
  }

  fs.mkdirSync(destDir, { recursive: true });
  let copied = 0;
  for (const name of MODEL_FILES) {
    const source = path.join(sourceDir, name);
    if (!fs.existsSync(source)) {
      console.error(`generate-models: missing ${name}, skipping`);
      continue;
    }
    fs.copyFileSync(source, path.join(destDir, name));
    copied++;
  }
  console.log(`generate-models: copied ${copied}/${MODEL_FILES.length} model files -> ${destDir}`);
}

main();
