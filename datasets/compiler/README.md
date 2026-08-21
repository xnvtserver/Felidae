# Compiler mixfix corpus

Canonical positive file: `mixfix-v1.jsonl`. Rejection-evaluation file:
`mixfix-invalid-v1.jsonl`.

The dataset is JSON Lines, with one versioned record per line:

```json
{"schema_version":1,"input_ids":[1,2],"target_ids":[3,0]}
```

The target sequence ends in `IR_END`. The output vocabulary is the parser's
fixed structural vocabulary; constants and symbols remain bounded references
resolved by the compiler context. `mixfix-v1.jsonl` is generated from valid,
deterministically resolved `v2_examples/` spans with
`felidae_extract_mixfix_dataset`, plus deterministic parser-validated
permutations of existing arithmetic, nested, and typed-fact mixfix forms.
Records whose teacher IR cannot fit the
fixed structural bounds are reported and excluded rather than truncated. No
trained mixfix artifact is currently shipped.

Invalid programs from `examples/invalid/` and `v2_examples/invalid/` are kept
in the separate integer-only rejection-evaluation set. Its records contain
`input_ids` and `rejection_stage` (`1` parser, `2` compiler, `3` verified
runtime); they never enter sequence-to-sequence GRU training because invalid
source has no safe IR target. Use them to measure that a compiler build rejects
malformed input at the intended boundary.

The trainer splits complete structural target families, not random records:
numeric variations of a generated mixfix template cannot leak into both train
and validation. It reports teacher-forced loss, autoregressive exact sequence
matches, and invalid autoregressive decodes. Small target families remain
training-only and are reported through the family and validation counts.

Regenerate both corpora together:

```text
build\\felidae_extract_mixfix_dataset.exe datasets\\compiler\\mixfix-v1.jsonl examples v2_examples --rejections datasets\\compiler\\mixfix-invalid-v1.jsonl
```
