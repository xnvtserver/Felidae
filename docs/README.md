# Felidae documentation

The supported product path is deliberately small:

```text
source.fx -> felidae_compiler -> verified FELBIR v16 executable IR -> felidae_vm
```

Build artifacts are created only in `build/`; the portable package is generated
only from a Release configuration by the `felidae_dist` CMake target in
`<build-directory>/dist/`. Use the repository
[`README.md`](../README.md) for build, compiler, VM, binary-format, mixfix,
and SSM guidance.

FELBIR contains the verified executable IR, bounded constants, complete
SentencePiece-sequence symbol and text entries, procedures, fact types, and
source maps. It contains no AST, source syntax, symbol hashes, or secondary
instruction representation.

The former interpreter-hosted documentation site, browser/WASM playground, and
Celidae visualizer were removed because they are not part of the FELBIR/Form VM
release path.
