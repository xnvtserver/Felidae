#!/usr/bin/env node
"use strict";
// Trains the Felidae ranking models and writes them to ml/models/.
//
//   cd ml && npm install && npm run train
//
// Models are checked in, so neither editor extension needs this pipeline (or
// any ML runtime) at install time - they only embed a small tree evaluator.
// Re-run after changing lib/features.js or after the .fx corpus grows.

const fs = require("fs");
const path = require("path");
const gbdt = require("./lib/gbdt");
const features = require("./lib/features");
const corpus = require("./lib/corpus");

const REPO_ROOT = path.resolve(__dirname, "..");
const MODEL_DIR = path.join(__dirname, "models");
const CORPUS_ROOTS = ["examples", "v2_examples", "core"].map((dir) => path.join(REPO_ROOT, dir));

// Files are split by hash of their path so the same file always lands in the
// same fold: a held-out set that shares files with training would report
// meaningless accuracy.
function isHeldOut(file) {
  let hash = 0;
  const key = path.relative(REPO_ROOT, file).replace(/\\/g, "/");
  for (let i = 0; i < key.length; i++) hash = (hash * 31 + key.charCodeAt(i)) & 0x7fffffff;
  return hash % 5 === 0; // ~20% held out
}

function flatten(groups) {
  const X = [];
  const y = [];
  for (const group of groups) {
    for (const row of group.rows) {
      X.push(row.features);
      y.push(row.label);
    }
  }
  return { X, y };
}

/**
 * Caps the number of training rows. The corpus yields ~600k completion rows,
 * far more than 13 features need, and ml-cart is an exact (non-histogram)
 * learner - fitting every round on all of them costs minutes per tree for no
 * measurable accuracy gain. Whole groups are kept or dropped so a group never
 * loses its positive, and selection is deterministic so training is
 * reproducible.
 */
function budgetGroups(groups, maxRows) {
  let total = 0;
  for (const group of groups) total += group.rows.length;
  if (total <= maxRows) return groups;

  const keepEvery = total / maxRows;
  const kept = [];
  let accumulated = 0;
  let emitted = 0;
  for (const group of groups) {
    accumulated += group.rows.length;
    if (accumulated >= emitted * keepEvery) {
      kept.push(group);
      emitted += group.rows.length;
      if (emitted >= maxRows) break;
    }
  }
  return kept;
}

function trainTask(name, featureNames, allTrainGroups, testGroups, options) {
  const trainGroups = budgetGroups(allTrainGroups, options.maxRows || 40000);
  const { X, y } = flatten(trainGroups);
  if (X.length === 0) {
    console.log(`  ${name}: no training rows, skipped`);
    return null;
  }
  const model = gbdt.train(X, y, options);
  const score = (row) => gbdt.predict(model, row);

  const test = flatten(testGroups);
  const trainMetrics = gbdt.rankingMetrics(trainGroups, score);
  const testMetrics = gbdt.rankingMetrics(testGroups, score);
  const testAuc = test.X.length ? gbdt.auc(test.y, test.X.map(score)) : 0;

  // The bar to beat: what the extension already did without a model. For
  // completion that is "keep prefix matches, otherwise no opinion"; for
  // next-param it is declaration order, which a constant score reproduces
  // because rankingMetrics sorts stably. Shipping a model that does not beat
  // this would be pure overhead.
  const baseline = gbdt.rankingMetrics(testGroups, options.baselineScore || (() => 0));

  console.log(
    `  ${name}: rows=${X.length} (of ${allTrainGroups.reduce((n, g) => n + g.rows.length, 0)}) ` +
      `trees=${model.trees.length}\n` +
      `      train top1=${(trainMetrics.top1 * 100).toFixed(1)}%\n` +
      `      test  top1=${(testMetrics.top1 * 100).toFixed(1)}% ` +
      `mrr=${testMetrics.mrr.toFixed(3)} auc=${testAuc.toFixed(3)} ` +
      `(groups=${testMetrics.groups})\n` +
      `      baseline top1=${(baseline.top1 * 100).toFixed(1)}% mrr=${baseline.mrr.toFixed(3)}`
  );

  return {
    task: name,
    featureNames,
    objective: model.objective,
    base: model.base,
    learningRate: model.learningRate,
    trees: model.trees,
    metrics: {
      testTop1: Number(testMetrics.top1.toFixed(4)),
      testMrr: Number(testMetrics.mrr.toFixed(4)),
      testAuc: Number(testAuc.toFixed(4)),
      baselineTop1: Number(baseline.top1.toFixed(4)),
      trainRows: X.length,
      testGroups: testMetrics.groups
    }
  };
}

function main() {
  const builtinDocs = JSON.parse(
    fs.readFileSync(path.join(REPO_ROOT, "docs", "builtin-docs.json"), "utf8")
  );

  const files = corpus.listFxFiles(CORPUS_ROOTS);
  if (files.length === 0) {
    console.error("train: no .fx files found under " + CORPUS_ROOTS.join(", "));
    process.exit(1);
  }
  const trainFiles = files.filter((file) => !isHeldOut(file));
  const testFiles = files.filter(isHeldOut);
  console.log(`corpus: ${files.length} .fx files (${trainFiles.length} train / ${testFiles.length} held out)`);

  // The index is built from training files only - deriving corpus frequencies
  // from held-out files would leak them into the reported metrics.
  const index = corpus.buildCorpusIndex(trainFiles, builtinDocs);
  console.log(
    `index: ${index.declarations.size} declarations, ` +
      `${Object.keys(index.builtinFreq).length} library names, ` +
      `${Object.keys(index.slotFreq).length} param slots`
  );

  console.log("training:");
  const models = {};

  models["completion-rank"] = trainTask(
    "completion-rank",
    features.COMPLETION_FEATURES,
    corpus.buildCompletionExamples(trainFiles, index),
    corpus.buildCompletionExamples(testFiles, index),
    {
      objective: "logistic",
      rounds: 60,
      learningRate: 0.15,
      maxDepth: 4,
      minNumSamples: 20,
      maxRows: 30000,
      // Prefix filtering only (feature 2 = isPrefixMatchCI), which is what an
      // unranked completion list effectively offers.
      baselineScore: (row) => row[2]
    }
  );

  models["next-param"] = trainTask(
    "next-param",
    features.NEXT_PARAM_FEATURES,
    corpus.buildNextParamExamples(trainFiles, index, builtinDocs),
    corpus.buildNextParamExamples(testFiles, index, builtinDocs),
    { objective: "logistic", rounds: 60, learningRate: 0.2, maxDepth: 3, minNumSamples: 8 }
  );

  fs.mkdirSync(MODEL_DIR, { recursive: true });

  // The runtime tables the features need, shipped alongside the trees.
  const runtimeIndex = {
    builtinFreq: index.builtinFreq,
    slotFreq: index.slotFreq
  };

  let written = 0;
  for (const [name, model] of Object.entries(models)) {
    if (!model) continue;
    const file = path.join(MODEL_DIR, `${name}.json`);
    fs.writeFileSync(file, JSON.stringify(model) + "\n");
    console.log(`  wrote ${path.relative(REPO_ROOT, file)} (${fs.statSync(file).size} bytes)`);
    written++;
  }
  const indexFile = path.join(MODEL_DIR, "corpus-index.json");
  fs.writeFileSync(indexFile, JSON.stringify(runtimeIndex) + "\n");
  console.log(`  wrote ${path.relative(REPO_ROOT, indexFile)} (${fs.statSync(indexFile).size} bytes)`);

  if (written === 0) {
    console.error("train: no models produced");
    process.exit(1);
  }
}

main();
