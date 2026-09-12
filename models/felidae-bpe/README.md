# Felidae word vocabulary model

`model.txt` is a line-oriented vocabulary. The physical line number is the
stable token ID, starting at zero. The first 59 lines are Felidae's fixed
grammar IDs and must not be reordered. `<0xNN>` denotes one byte so that
whitespace and arbitrary UTF-8 remain reversible. New identifier words are
appended deterministically by the runtime.

The initial word entries are a small checked-in text-dictionary seed. The
runtime extends the same table from source. Despite the directory's name,
this is a deterministic whole-word dictionary lookup, not byte-pair encoding
(see `src/Tokenizer.h` for the full explanation) - it has no training step
and no WordNet or SentencePiece dependency.
