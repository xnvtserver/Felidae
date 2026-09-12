# Felidae datasets

The tracked datasets contain deterministic tokenizer and reasoning examples.
Felidae does not train or load a compiler, VM, statistical parser, or runtime
state model. Generated output and temporary probes belong under `build/`.

Dataset changes must preserve their documented schema and include validation
against the authoritative tokenizer and interpreter.
