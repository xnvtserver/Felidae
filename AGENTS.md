# Repository working rules

- Put every generated build artifact and CMake build tree under `build/`.
- Use named subdirectories such as `build/debug`, `build/asan`, and
  `build/release` when configurations need isolation.
- Do not create peer directories such as `build-debug`, `build-clang`, or
  `build-sentencepiece` at the repository root.
- Keep source, documentation, and explicitly generated model files in their
  repository-defined locations; do not redirect unrelated output into the
  source tree.
- Do not execute commands expected to take a long time, including clean native
  builds, complete test suites, training, packaging, or benchmarks. Provide
  the exact command to the user, let the user run it, and diagnose the final
  output they return. Short configuration checks and focused diagnostics are
  allowed.
- Do not repeatedly poll or stream routine build progress. Ask for the final
  failure block or success summary to avoid wasting context and tokens.
- Keep compiler, VM, model training, checkpointing, and TorchScript export in
  C++. Do not introduce Python scripts or Python subprocesses into the build,
  training, inference, testing, or model-export pipeline.
