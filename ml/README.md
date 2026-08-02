# felidae-ml

Offline training pipeline for the ranking models that make the Felidae editor
extensions smarter. **Nothing here ships to users** — the extensions embed only
a small tree evaluator and the checked-in JSON models.

## What it produces

| Model | Used for | Held-out top-1 | Baseline |
|---|---|---|---|
| `models/completion-rank.json` | ordering the completion popup | **83.7%** | 43.7% (prefix filter only) |
| `models/next-param.json` | ordering `key:` suggestions inside a call | **95.6%** | 86.9% (declaration order) |
| `models/corpus-index.json` | frequency tables the features need | — | — |

Baselines are what the extensions did *before* ranking, measured on the same
held-out files, so the comparison reflects a real user-visible change rather
than a number in isolation.

## Algorithm

Second-order gradient boosting (the XGBoost/LightGBM formulation) over
depth-limited regression trees, with LightGBM's histogram split finding —
features are pre-binned into quantile buckets once, so each split scan is
O(rows + bins).

Split finding is implemented in `lib/gbdt.js` rather than taken from a library
because the JS options do not work at this scale: `ml-cart` (the ml.js CART, the
only maintained pure-JS one) is effectively quadratic — measured **5.3s for
2,000 rows and 24.4s for 5,000** on this corpus, i.e. minutes per tree — and
`ml-xgboost`, the WASM XGBoost port, loads on Node 24 but exports nothing
usable. Native LightGBM/XGBoost bindings would pull per-platform binaries into
a repository whose whole point here is that no ML runtime reaches users. The
output is the same canonical tree ensemble either library would produce.

## Training data

Supervision is free and comes from the repository's own `.fx` sources
(`examples/`, `v2_examples/`, `core/` — 285 files):

- **Completion**: at every identifier the corpus writes, that token *is* the
  correct completion and every other in-scope name is a negative.
- **Next param**: the key actually written next is the positive among the
  call's remaining parameters.

Files are split by a hash of their path, so ~20% are held out and the same file
always lands in the same fold. The corpus frequency tables are built from
training files only — deriving them from held-out files would leak into the
reported metrics.

## Running it

```sh
cd ml
npm install
npm run train          # rewrites models/ and prints held-out metrics
```

Then refresh the copies the extensions bundle:

```sh
cd ../vs-code-extension && npm run generate:models
cd ../intellij-idea-extension && ./gradlew generateMlModels
```

(Both also run automatically as part of their normal builds.)

## Parity — the thing that must not silently break

The feature vectors are computed three times: here (`lib/features.js`), in
`vs-code-extension/src/mlRanking.ts`, and in
`intellij-idea-extension/.../ml/FelidaeMlRanking.java`. A mismatch does not
fail loudly — it silently scores noise. Two checks guard this:

```sh
node verify-parity.js        # TypeScript scorer vs this pipeline
node verify-parity-java.js   # Java scorer vs this pipeline (needs a JDK)
```

Both compare predictions over thousands of pseudo-random vectors and currently
agree to machine epsilon (max delta 2.2e-16). **Run them after touching
`lib/gbdt.js`, `lib/features.js`, or either scorer.**

## Layout

```
lib/gbdt.js      boosting loop, histogram tree learner, metrics
lib/features.js  feature definitions (the contract with both scorers)
lib/corpus.js    .fx corpus -> supervised examples
train.js         driver: split, train, evaluate, write models
verify-parity.js / verify-parity-java.js
java/ParityHarness.java   stdin harness used by the Java parity check
models/          checked-in output
```
