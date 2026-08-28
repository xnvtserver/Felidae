# Compiler mixfix corpus

Canonical positive file: `mixfix-v1.jsonl`. Rejection-evaluation file:
`mixfix-invalid-v1.jsonl`.

The dataset is JSON Lines, with one versioned record per line:

```json
{"schema_version":3,"sentencepiece_model_identity":"sha256:...","compiler_ir_vocabulary":"felidae-compiler-ir-v4","decision":"ACCEPT","input_ids":[1,2],"target_ids":[3,0]}
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

Structurally valid spans that require model target selection receive a single
`ABSTAIN` teacher token instead of fabricated executable IR. Invalid programs
receive `REJECT`; deterministic teachers begin with `ACCEPT`. Complete REJECT
and ABSTAIN families are kept in the training partition so both decisions are
actually learned, while executable target families are isolated across train,
validation, and test partitions.

`input_ids` are meaningful only for the exact fixed tokenizer that produced
them. Every record therefore carries `sentencepiece_model_identity`; the C++
trainer rejects stale corpora after any tokenizer regeneration.
`compiler_ir_vocabulary` similarly prevents training against target IDs from
an older internal compiler-IR opcode layout. It does not change the stable
Felidae executable-IR opcode IDs.

Current invalid programs named `v2_examples/invalid_*.fx` are kept in the
separate integer-only rejection-evaluation set. Its records contain
`input_ids` and `rejection_stage` (`1` parser, `2` compiler, `3` verified
runtime), an explicit `REJECT` target, and no executable compiler IR. They are
real negative training records: the model learns the bounded rejection
decision without receiving fabricated opcodes. Use them also to measure that a
compiler build rejects malformed input at the intended boundary.

The trainer splits complete structural target families, not random records:
numeric variations of a generated mixfix template cannot leak into both train
and validation. It reports teacher-forced loss, autoregressive exact sequence
matches, and invalid autoregressive decodes. Small target families remain
training-only and are reported through the family and validation counts.

Regenerate both corpora together:

```text
cmake --build build/debug --target felidae_extract_mixfix_dataset --parallel 1
./build/debug/felidae_extract_mixfix_dataset datasets/compiler/mixfix-v1.jsonl examples v2_examples --rejections datasets/compiler/mixfix-invalid-v1.jsonl
```
