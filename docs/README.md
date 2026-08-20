# Felidae documentation

The supported product path is deliberately small:

```text
source.fx -> felidae_compiler -> verified FELBIN v8 .bin -> felidae_vm
```

Build artifacts are created only in `build/`; the portable package is generated
by the `felidae_dist` CMake target in `dist/`. Use the repository
[`README.md`](../README.md) for build, compiler, VM, binary-format, mixfix,
and SSM guidance.

The former interpreter-hosted documentation site, browser/WASM playground, and
Celidae visualizer were removed because they are not part of the FELBIN/Form VM
release path.
