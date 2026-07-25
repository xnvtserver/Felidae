#!/usr/bin/env node
// Regenerates the library-name list embedded in syntaxes/felidae.tmLanguage.json
// from `celidae --list-libraries`, so the grammar never hand-copies a stale
// module list. TextMate grammars are static JSON loaded once by VS Code, so
// this runs at build/package time rather than on every keystroke.
//
// Usage: node scripts/generate-libraries.js [path/to/celidae]
// Falls back to build/celidae(.exe) or build/felidae_debug(.exe) relative to
// the repo root when no path is given.

"use strict";

const fs = require("fs");
const path = require("path");
const { spawnSync } = require("child_process");

function findCelidae(repoRoot) {
  const candidates = [
    process.argv[2],
    path.join(repoRoot, "build", "celidae"),
    path.join(repoRoot, "build", "celidae.exe"),
    path.join(repoRoot, "build", "felidae_debug"),
    path.join(repoRoot, "build", "felidae_debug.exe"),
  ].filter(Boolean);

  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) return candidate;
  }
  return null;
}

function main() {
  const repoRoot = path.resolve(__dirname, "..", "..");
  const celidae = findCelidae(repoRoot);

  if (!celidae) {
    console.error(
      "generate-libraries: no celidae/felidae_debug executable found. " +
        "Build the project first (./build.sh) or pass an explicit path. " +
        "Leaving syntaxes/felidae.tmLanguage.json untouched."
    );
    process.exit(0); // Non-fatal: keep the last generated/committed list.
  }

  const result = spawnSync(celidae, ["--list-libraries"], { encoding: "utf8" });
  if (result.status !== 0 || !result.stdout) {
    console.error(
      `generate-libraries: '${celidae} --list-libraries' failed: ${result.stderr || result.status}`
    );
    process.exit(0);
  }

  let libraries;
  try {
    libraries = JSON.parse(result.stdout.trim());
  } catch (error) {
    console.error(`generate-libraries: could not parse celidae output as JSON: ${error.message}`);
    process.exit(0);
  }

  if (!Array.isArray(libraries) || libraries.length === 0) {
    console.error("generate-libraries: celidae reported no libraries; leaving grammar untouched.");
    process.exit(0);
  }

  libraries = [...libraries].sort();

  const grammarPath = path.join(__dirname, "..", "syntaxes", "felidae.tmLanguage.json");
  const grammar = JSON.parse(fs.readFileSync(grammarPath, "utf8"));

  const alternation = libraries.join("|");
  grammar.repository.libraryCalls.patterns[0].match = `\\b(?:${alternation})(?=\\s*[:.])`;

  fs.writeFileSync(grammarPath, JSON.stringify(grammar, null, 2) + "\n");
  console.log(`generate-libraries: wrote ${libraries.length} library names from ${celidae}.`);
}

main();
