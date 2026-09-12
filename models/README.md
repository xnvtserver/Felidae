# Felidae token model

`felidae-bpe/model.txt` is the only tokenizer model. It is line-oriented:
line N is token ID N. The first 59 entries are fixed syntax IDs declared in
`src/FelidaeTokenizerIds.h`; do not reorder them.

The remaining entries form a simple checked-in text dictionary. The normal
lexer owns comments, strings, numbers, punctuation, operators, and reserved
words such as `class`, `extends`, `index`, and `end`; BPE looks up only
identifiers and mixfix anchors, then appends missing
identifiers in source order. No model training, generated binary model,
WordNet synset database, or external tokenizer dependency is needed.
