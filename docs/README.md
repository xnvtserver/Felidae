# Felidae documentation

The supported product path is deliberately small:

```text
source.fx -> felidae_compiler -> verified FELBIN v10 / Felidae ISA v1 .bin -> felidae_vm
```

Build artifacts are created only in `build/`; the portable package is generated
only from a Release configuration by the `felidae_dist` CMake target in
`<build-directory>/dist/`. Use the repository
[`README.md`](../README.md) for build, compiler, VM, binary-format, mixfix,
and SSM guidance.

Compiler IR and SentencePiece IDs are frontend-only. FELBIN contains verified
ISA words plus bounded constant, UTF-8 text, symbol, procedure, fact-type, and
source-map tables; neither compiler IR nor tokenizer IDs are executable data.

The former interpreter-hosted documentation site, browser/WASM playground, and
Celidae visualizer were removed because they are not part of the FELBIN/Form VM
release path.
