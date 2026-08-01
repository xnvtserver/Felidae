#!/usr/bin/env node
// Copies the repo-root docs/builtin-docs.json (the single source of truth for
// stdlib hover-doc content, shared with the IntelliJ plugin) into
// resources/builtin-docs.json so it gets bundled into the packaged extension.
// Runs at build/package time, same as generate-libraries.js.

"use strict";

const fs = require("fs");
const path = require("path");

function main() {
  const repoRoot = path.resolve(__dirname, "..", "..");
  const sourcePath = path.join(repoRoot, "docs", "builtin-docs.json");
  const destPath = path.join(__dirname, "..", "resources", "builtin-docs.json");

  if (!fs.existsSync(sourcePath)) {
    console.error(
      `generate-builtin-docs: ${sourcePath} not found. Leaving resources/builtin-docs.json untouched.`
    );
    process.exit(0);
  }

  let docs;
  try {
    docs = JSON.parse(fs.readFileSync(sourcePath, "utf8"));
  } catch (error) {
    console.error(`generate-builtin-docs: could not parse ${sourcePath}: ${error.message}`);
    process.exit(0);
  }

  fs.mkdirSync(path.dirname(destPath), { recursive: true });
  fs.writeFileSync(destPath, JSON.stringify(docs, null, 2) + "\n");
  console.log(`generate-builtin-docs: wrote ${Object.keys(docs).length} entries -> ${destPath}`);
}

main();
