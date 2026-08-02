#!/usr/bin/env node
"use strict";
// Checks that the runtime scorers reproduce the training-time model exactly.
//
//   cd ml && node verify-parity.js
//
// The TS and Java scorers each reimplement tree evaluation and the feature
// vectors so that no ML runtime has to ship inside an extension. That is only
// safe if all three implementations agree bit-for-bit, so this compares them
// on the real models over pseudo-random feature vectors, and additionally
// checks the extensions' compiled scorer against ml/lib/gbdt.js.
//
// The Java side is checked by ml/verify-parity-java.js, which shells out to a
// tiny harness; this file covers JS/TS and is the one to run after touching
// lib/gbdt.js or lib/features.js.

const fs = require("fs");
const path = require("path");
const gbdt = require("./lib/gbdt");

const REPO_ROOT = path.resolve(__dirname, "..");
const MODEL_DIR = path.join(__dirname, "models");
const TS_SCORER = path.join(REPO_ROOT, "vs-code-extension", "out", "mlRanking.js");

function loadTsScorer() {
  if (!fs.existsSync(TS_SCORER)) return null;
  const Module = require("module");
  const original = Module._resolveFilename;
  Module._resolveFilename = function (request, ...rest) {
    if (request === "vscode") return path.join(__dirname, "vscode-shim.js");
    return original.call(this, request, ...rest);
  };
  try {
    return require(TS_SCORER);
  } finally {
    Module._resolveFilename = original;
  }
}

function randomVectors(count, width, seed) {
  let state = seed;
  const next = () => (state = (state * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff;
  const vectors = [];
  for (let i = 0; i < count; i++) {
    const row = [];
    for (let f = 0; f < width; f++) {
      // Mix small integers, ratios and log-scaled counts so the probes land on
      // both sides of realistic thresholds.
      const draw = next();
      row.push(draw < 0.3 ? Math.floor(next() * 8) : draw < 0.6 ? next() : Math.log1p(Math.floor(next() * 400)));
    }
    vectors.push(row);
  }
  return vectors;
}

function main() {
  let failures = 0;
  const ts = loadTsScorer();
  if (!ts) {
    console.error("verify-parity: vs-code-extension/out/mlRanking.js not built; run npm run compile there first");
    process.exit(1);
  }

  for (const name of ["completion-rank", "next-param"]) {
    const file = path.join(MODEL_DIR, `${name}.json`);
    if (!fs.existsSync(file)) {
      console.log(`  ${name}: model missing, skipped`);
      continue;
    }
    const model = JSON.parse(fs.readFileSync(file, "utf8"));
    const width = model.featureNames.length;
    const vectors = randomVectors(4000, width, 987654321);

    let maxDelta = 0;
    for (const vector of vectors) {
      const reference = gbdt.predict(model, vector);
      const runtime = ts.__predictForParity(model, vector);
      maxDelta = Math.max(maxDelta, Math.abs(reference - runtime));
    }
    const ok = maxDelta < 1e-12;
    if (!ok) failures++;
    console.log(
      `  ${name}: ${ok ? "ok" : "MISMATCH"} over ${vectors.length} vectors ` +
        `(max |delta| = ${maxDelta.toExponential(2)})`
    );
  }

  console.log(failures === 0 ? "parity: all scorers agree" : `parity: ${failures} model(s) disagree`);
  process.exit(failures === 0 ? 0 : 1);
}

main();
