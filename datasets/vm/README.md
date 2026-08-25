# VM SSM corpus

The current reusable baseline is `runtime-context-v1.jsonl`. It is generated
from verified deterministic `.bin` results built from `v2_examples`.

The dataset is JSON Lines schema v7, with one operation-level record per line:
`operation_id`, ordered `input_kinds`, sorted `fact_types`, sorted
`fact_type_counts`, sorted `hierarchy_edges`, `target_kind`, and `target_value`. Those fields are exactly
the finite information the current GRU sees at inference; it must not store
whole-program results or train-only features.
`target_value` must name an action in the production vocabulary: input/fact
references `0..15`, numeric truth `0` or `1`, nil `0`, or Degree milli-values
`0`, `250`, `500`, `750`, and `1000`.

The baseline trains only the permanent `SemanticOperationId::Identity` value
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
