# VM SSM corpus

`runtime-context-v1.jsonl` is generated from verified deterministic `.bin`
results built from `v2_examples`. Regenerate it with the current
`felidae_build_runtime_dataset` whenever the tokenizer or this schema changes;
schema-v8 records are intentionally rejected rather than silently training a
model with different inputs.

The dataset is JSON Lines schema v9, with one operation-level record per line:
`operation_id`, ordered `input_kinds`, canonical `input_values`, sorted
`fact_types`, sorted `fact_type_counts`, sorted `hierarchy_edges`,
`target_kind`, and either `target_value` or `target_score`. Those fields are
exactly the bounded information the current GRU sees at inference; it must not
store whole-program results or train-only features.
Stable `input_kinds` values are nil `1`, number `2`, Degree `3`, text `4`,
array `5`, map `6`, fact `7`, symbol `8`, tensor `9`, and dynamic text-keyed
map `10`.
`input_values` stores SentencePiece IDs directly and uses the documented
reserved structural-token tail for value boundaries, kinds, fields, and exact
floating-point bits. Training and live inference use the same encoder.
Every symbol in the three fact/hierarchy fields is its complete SentencePiece
ID sequence. Module-local indexes, hashes, and source spellings are not model
identity and must never be written to this dataset.
For finite action teachers, `target_value` must name an action in the production vocabulary: input/fact
references `0..15`, numeric truth `0` or `1`, nil `0`, or Degree milli-values
`0`, `250`, `500`, `750`, and `1000`.
An `ssm.suggest` teacher instead uses the score target kind and a finite
`target_score`; validation reports mean absolute error separately from action
accuracy.

The baseline currently contains 15 deterministic records across the existing
typed runtime fixtures and trains only the permanent `SemanticOperationId::Identity` value
`0x0001`. It preserves one typed input and never hashes a source spelling into
an operation ID. Binaries containing `SemanticEval` still require a separate
explicit teacher and are rejected rather than assigned a whole-program label.
No runtime model is shipped until the broader fact/hierarchy corpus has been
trained and validated.

`fact_type_counts` is the bounded population observed for each fact type. The
runtime GRU encodes it as one / two-to-four / five-or-more, so it can
distinguish sparse from populated knowledge without serializing fact fields or
converting facts to source text.

Training uses a fixed-seed structural split by `(operation_id, ordered input
kinds, target action)`. Repeated records from one structural family cannot
appear in both train and validation; the trainer prints per-family held-out
accuracy. This is a baseline measurement, not evidence that the current small
corpus generalizes to arbitrary fact reasoning.

Invalid `.fx` examples belong to compiler rejection evaluation: invalid source
never reaches a verified `.bin` or the VM SSM. VM datasets therefore contain
only verified operation records with safe, explicit labels.
