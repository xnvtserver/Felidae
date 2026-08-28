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
- Before adding a helper, representation, parser path, validation, or other
  logic, search for an existing implementation and reuse or simplify it.
  Prefer correcting and consolidating existing code over creating parallel
  mechanisms.
- Avoid speculative abstractions, duplicate checks, and defensive validation
  inside already verified or type-safe code. Validate once at genuine trust
  boundaries, then rely on the established invariant unless evidence requires
  another check.
- Optimize first for clear, maintainable, correct code. Add complexity or
  performance-specific logic only when a measured requirement justifies it.
- Keep contracts unambiguous and stable: document non-obvious identity,
  ownership, lifetime, binary-format, and model-input invariants beside their
  authoritative types or functions. Do not implement behavior from intuition
  when a contract can be stated and tested.
- Apply DRY to behavior, not merely syntax. Maintain one authoritative path
  for each conversion, verification, encoding, and execution rule. Remove a
  stale path only after confirming its callers and required behavior have
  moved to that path.
- Ask the user before implementing an unresolved choice that materially
  changes semantics, data formats, ownership, compatibility, or performance
  tradeoffs. Continue autonomously for mechanical corrections whose intended
  behavior is already established by the repository contract.
- Before regenerating a checked-in model, dataset, vocabulary header, or other
  generated source artifact, inspect its Git status and relevant history plus
  the generator inputs and dependencies. Regenerate only when an authoritative
  input changed or the user explicitly requests it; confirm whether the result
  actually changed before keeping it.
- Treat dependency changes made by Dependabot as protected. Do not downgrade,
  remove, revert, or replace them during compatibility fixes or cleanup. Move
  fixes forward from the Dependabot-selected versions, and ask the user before
  changing a dependency pin or deleting dependency/submodule remnants.
