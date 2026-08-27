# Generated models

`felidae.model` and `FelidaeSentencePieceIds.h` are versioned together because
their PieceIds form part of the executable IR contract. Normal builds consume
them without rewriting them.

After reviewing the relevant Git history and changing an authoritative corpus
or grammar input, regenerate them explicitly with:

```sh
cmake --build build/debug --target felidae_regenerate_sentencepiece_model --parallel 8
```

Keep the result only when both generated artifacts are intentional and their
tests pass. Versioned LibTorch GRU artifacts and manifests also belong here.
They remain outside `build/`, which is reserved for build products.
