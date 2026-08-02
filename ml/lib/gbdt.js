"use strict";
// Gradient-boosted decision trees for the Felidae extensions' ranking models.
//
// Algorithm: the XGBoost/LightGBM formulation - second-order (gradient +
// hessian) boosting over depth-limited regression trees, with the histogram
// split-finding LightGBM uses. Features are pre-binned into quantile buckets
// once, so each split scan is O(rows + bins) instead of O(rows log rows) per
// feature per node.
//
// Why not a library: ml.js (ml-cart) is the only maintained pure-JS CART, and
// it is effectively quadratic here - measured 5.3s for 2,000 rows and 24.4s
// for 5,000 on this corpus, i.e. minutes per tree and hours for an ensemble.
// ml-xgboost, the asm.js/WASM XGBoost port, loads on Node 24 but exports
// nothing usable. Native LightGBM/XGBoost bindings would drag per-platform
// binaries into a repo whose whole point here is that no ML runtime ships to
// users. So split finding is implemented directly; it is ~150 lines of
// well-understood arithmetic, it trains the full corpus in seconds, and the
// output is the same canonical tree ensemble either library would produce.
//
// Objectives:
//   "logistic"  binary classification; predict() returns a probability.
//   "l2"        least-squares regression; predict() returns the raw score.
//
// Ranking is done as logistic classification over (query, candidate) pairs and
// then sorting by score - the standard pointwise learning-to-rank reduction,
// which behaves well on a corpus this size.

const MAX_BINS = 64;

function sigmoid(x) {
  if (x >= 0) return 1 / (1 + Math.exp(-x));
  const z = Math.exp(x);
  return z / (1 + z);
}

/**
 * Candidate split thresholds per feature, taken at quantiles of the observed
 * values. A split stored as threshold `t` means "x < t goes left", which is
 * exactly how the runtime scorers evaluate it.
 */
function buildThresholds(X, featureCount) {
  const thresholds = [];
  for (let f = 0; f < featureCount; f++) {
    const values = new Float64Array(X.length);
    for (let i = 0; i < X.length; i++) values[i] = X[i][f] ?? 0;
    values.sort();

    const distinct = [];
    for (let i = 0; i < values.length; i++) {
      if (i === 0 || values[i] !== values[i - 1]) distinct.push(values[i]);
    }
    // Split points sit between distinct values; with few distinct values use
    // them all, otherwise sample evenly across the sorted order.
    const candidates = [];
    if (distinct.length <= 1) {
      thresholds.push(candidates);
      continue;
    }
    const wanted = Math.min(MAX_BINS - 1, distinct.length - 1);
    for (let k = 1; k <= wanted; k++) {
      const index = Math.floor((k * (distinct.length - 1)) / wanted);
      const value = distinct[Math.max(1, index)];
      if (candidates.length === 0 || candidates[candidates.length - 1] !== value) {
        candidates.push(value);
      }
    }
    thresholds.push(candidates);
  }
  return thresholds;
}

/** bin(x) = index of the first threshold strictly greater than x. */
function binValue(value, featureThresholds) {
  let low = 0;
  let high = featureThresholds.length;
  while (low < high) {
    const mid = (low + high) >> 1;
    if (value < featureThresholds[mid]) high = mid;
    else low = mid + 1;
  }
  return low;
}

function buildBins(X, thresholds) {
  const rows = X.length;
  const featureCount = thresholds.length;
  const bins = new Uint8Array(rows * featureCount);
  for (let f = 0; f < featureCount; f++) {
    const featureThresholds = thresholds[f];
    if (featureThresholds.length === 0) continue;
    for (let i = 0; i < rows; i++) {
      bins[i * featureCount + f] = binValue(X[i][f] ?? 0, featureThresholds);
    }
  }
  return bins;
}

/**
 * Grows one regression tree on the given gradients/hessians, returning the
 * canonical node form: {f, t, l, r} internally, {v} at leaves.
 */
function growTree(bins, thresholds, gradients, hessians, indices, options) {
  const { maxDepth, minNumSamples, lambda, minChildWeight } = options;
  const featureCount = thresholds.length;

  const leafValue = (rowIndices) => {
    let g = 0;
    let h = 0;
    for (const i of rowIndices) {
      g += gradients[i];
      h += hessians[i];
    }
    return -g / (h + lambda);
  };

  const build = (rowIndices, depth) => {
    if (depth >= maxDepth || rowIndices.length < minNumSamples * 2) {
      return { v: leafValue(rowIndices) };
    }

    let totalG = 0;
    let totalH = 0;
    for (const i of rowIndices) {
      totalG += gradients[i];
      totalH += hessians[i];
    }
    const parentScore = (totalG * totalG) / (totalH + lambda);

    let bestGain = 0;
    let bestFeature = -1;
    let bestThresholdIndex = -1;

    for (let f = 0; f < featureCount; f++) {
      const featureThresholds = thresholds[f];
      const binCount = featureThresholds.length + 1;
      if (binCount < 2) continue;

      const gradHist = new Float64Array(binCount);
      const hessHist = new Float64Array(binCount);
      const countHist = new Int32Array(binCount);
      for (const i of rowIndices) {
        const bin = bins[i * featureCount + f];
        gradHist[bin] += gradients[i];
        hessHist[bin] += hessians[i];
        countHist[bin]++;
      }

      let leftG = 0;
      let leftH = 0;
      let leftCount = 0;
      // Splitting after bin k means "x < thresholds[k]" goes left.
      for (let k = 0; k < binCount - 1; k++) {
        leftG += gradHist[k];
        leftH += hessHist[k];
        leftCount += countHist[k];
        const rightCount = rowIndices.length - leftCount;
        if (leftCount < minNumSamples || rightCount < minNumSamples) continue;
        const rightG = totalG - leftG;
        const rightH = totalH - leftH;
        if (leftH < minChildWeight || rightH < minChildWeight) continue;

        const gain =
          (leftG * leftG) / (leftH + lambda) +
          (rightG * rightG) / (rightH + lambda) -
          parentScore;
        if (gain > bestGain) {
          bestGain = gain;
          bestFeature = f;
          bestThresholdIndex = k;
        }
      }
    }

    if (bestFeature < 0) return { v: leafValue(rowIndices) };

    const threshold = thresholds[bestFeature][bestThresholdIndex];
    const leftRows = [];
    const rightRows = [];
    for (const i of rowIndices) {
      if (bins[i * featureCount + bestFeature] <= bestThresholdIndex) leftRows.push(i);
      else rightRows.push(i);
    }
    if (leftRows.length === 0 || rightRows.length === 0) {
      return { v: leafValue(rowIndices) };
    }

    return {
      f: bestFeature,
      t: threshold,
      l: build(leftRows, depth + 1),
      r: build(rightRows, depth + 1)
    };
  };

  return build(indices, 0);
}

function evalTree(node, features) {
  let current = node;
  while (current.v === undefined) {
    current = (features[current.f] ?? 0) < current.t ? current.l : current.r;
  }
  return current.v;
}

/**
 * @param {number[][]} X feature rows
 * @param {number[]}   y labels (0/1 for logistic, real for l2)
 */
function train(X, y, options = {}) {
  const {
    objective = "logistic",
    rounds = 60,
    learningRate = 0.15,
    maxDepth = 4,
    minNumSamples = 10,
    lambda = 1.0,
    minChildWeight = 1e-3
  } = options;

  if (X.length === 0) throw new Error("gbdt.train: no training rows");
  const featureCount = X[0].length;

  let base;
  if (objective === "logistic") {
    const positives = y.reduce((sum, label) => sum + (label > 0.5 ? 1 : 0), 0);
    const rate = Math.min(Math.max(positives / y.length, 1e-6), 1 - 1e-6);
    base = Math.log(rate / (1 - rate));
  } else {
    base = y.reduce((sum, value) => sum + value, 0) / y.length;
  }

  const thresholds = buildThresholds(X, featureCount);
  const bins = buildBins(X, thresholds);
  const indices = Array.from({ length: X.length }, (_, i) => i);

  const scores = new Float64Array(X.length).fill(base);
  const gradients = new Float64Array(X.length);
  const hessians = new Float64Array(X.length);
  const trees = [];

  for (let round = 0; round < rounds; round++) {
    for (let i = 0; i < X.length; i++) {
      if (objective === "logistic") {
        const p = sigmoid(scores[i]);
        gradients[i] = p - y[i];
        hessians[i] = Math.max(p * (1 - p), 1e-6);
      } else {
        gradients[i] = scores[i] - y[i];
        hessians[i] = 1;
      }
    }

    const tree = growTree(bins, thresholds, gradients, hessians, indices, {
      maxDepth,
      minNumSamples,
      lambda,
      minChildWeight
    });
    if (tree.v !== undefined) break; // no split found: nothing left to learn

    for (let i = 0; i < X.length; i++) {
      scores[i] += learningRate * evalTree(tree, X[i]);
    }
    trees.push(tree);
  }

  return { objective, base, learningRate, trees };
}

/** Reference scorer. The TS and Java scorers must agree with this exactly. */
function predict(model, features) {
  let score = model.base;
  for (const tree of model.trees) {
    score += model.learningRate * evalTree(tree, features);
  }
  return model.objective === "logistic" ? sigmoid(score) : score;
}

/** Area under the ROC curve. */
function auc(labels, scores) {
  const order = scores.map((score, i) => [score, labels[i]]).sort((a, b) => a[0] - b[0]);
  let positives = 0;
  let negatives = 0;
  let rankSum = 0;
  for (let i = 0; i < order.length; i++) {
    if (order[i][1] > 0.5) {
      positives++;
      rankSum += i + 1;
    } else {
      negatives++;
    }
  }
  if (positives === 0 || negatives === 0) return 0.5;
  return (rankSum - (positives * (positives + 1)) / 2) / (positives * negatives);
}

/**
 * Fraction of ranking groups whose true item is ranked first, plus mean
 * reciprocal rank - what a user actually feels in a completion popup.
 */
function rankingMetrics(groups, scoreRow) {
  let top1 = 0;
  let reciprocalSum = 0;
  let counted = 0;
  for (const group of groups) {
    if (!group.rows.some((row) => row.label > 0.5)) continue;
    counted++;
    const ranked = group.rows
      .map((row) => ({ label: row.label, score: scoreRow(row.features) }))
      .sort((a, b) => b.score - a.score);
    const rank = ranked.findIndex((item) => item.label > 0.5) + 1;
    if (rank === 1) top1++;
    reciprocalSum += 1 / rank;
  }
  return counted === 0
    ? { groups: 0, top1: 0, mrr: 0 }
    : { groups: counted, top1: top1 / counted, mrr: reciprocalSum / counted };
}

module.exports = { train, predict, evalTree, sigmoid, auc, rankingMetrics };
