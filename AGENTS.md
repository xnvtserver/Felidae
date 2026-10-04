# Repository working rules

- Put every generated build artifact and CMake build tree under `build/`.
- Use named subdirectories such as `build/debug`, `build/asan`, and
  `build/release` when configurations need isolation.
- Do not create peer directories such as `build-debug`, `build-clang`, or
  other `build-*` trees at the repository root.
- Keep source, documentation, and explicitly generated model files in their
  repository-defined locations; do not redirect unrelated output into the
  source tree.
- Do not execute commands expected to take a long time, including clean native
  builds, complete test suites, training, packaging, or benchmarks. Provide
  the exact command to the user, let the user run it, and diagnose the final
  output they return. Short configuration checks and focused diagnostics are
  allowed.
- Test interpreter and language behavior primarily with Felidae programs. Put
  temporary diagnostic `.fx` probes under `build/` and promote only stable,
  intentional regressions to `tests/`; do not scatter ad-hoc C++ test files
  through the repository.
- When a current `felidae` executable is available, run focused `.fx` probes
  through the interpreter before adding or changing C++ tests. Use `--debug`
  to inspect Felidae goal execution and a native C++ debugger when the fault is
  below the language boundary. Compare both views when diagnosing parser,
  interpreter, fact-store, inheritance, or debugger behavior.
- Reserve C++ unit tests for mature internal contracts that cannot be observed
  reliably through a Felidae program. Do not use native compilation as the
  default feedback loop for language-level behavior.
- Do not repeatedly poll or stream routine build progress. Ask for the final
  failure block or success summary to avoid wasting context and tokens.
- When a build/test command is backgrounded, do not block-wait on the task and
  scan its full output. Redirect it to a log file, and once it's done, read
  only the tail (and grep for `error`/`FAILED` if needed). Scanning entire
  build logs (compiler warnings, dependency output, etc.) wastes context for
  no benefit — the tail plus an error grep is enough to know what happened.
- Keep the interpreter, native runtime, RocksDB integration, and native
  packages in C++. Do not introduce Python scripts or Python subprocesses
  into the build, execution, testing, or storage pipeline.
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
- Keep syntax atoms and data atoms distinct. Syntax atoms are interned parser,
  mixfix, and operator anchors; they are grammar metadata and are not persisted
  merely because they occur in syntax. Data atoms are immutable first-class
  symbolic runtime values selected by grammar context. They may be bound by an
  explicitly declared `def`, passed, returned, unified, indexed, and persisted
  in facts. An atom is distinct from a string with the same spelling. Persist
  atom text with an explicit atom type tag and re-intern it when reading; never
  use a process-local `SymbolId`, token ID, or `PatternId` as durable identity.
  External strings remain strings unless source syntax, a schema, or an
  explicit conversion requests an atom. Class and function references continue
  to use `Name.class` and `name.function`.
- Preserve Felidae's Erlang-inspired surface contract without casing rules:
  `def` and grammar context distinguish bindings, fact patterns, persistent
  seeds, and callables; an unresolved bare identifier in value position is a
  data atom. `.` terminates a statement and `end` terminates a block.
- `def name := value.` and `def name: Type := value.` create immutable
  interpreter bindings. `def name: optional<T | U>.` may omit its initializer
  and becomes nil; an untyped `def name.` is ambiguous and invalid. Top-level
  `def Type(...).` is a persistent seed, while the same form inside a rule is
  a fact pattern. Legacy `var` and declarations without `def` are errors.
- Class fields also require `def`, for example `def id: string.` or
  `def enabled: bool := false.` Structural class directives such as `key(...)`
  and `index(...)` remain unprefixed.
- `:=` is the binding/assignment operator. A single `=` is equality comparison
  (alongside `!=` for inequality) and must never be accepted as assignment.
- `is_atom(value)` is the canonical pure atom predicate. It is true only for a
  runtime data atom, never for a string, syntax anchor, number, class reference,
  or function reference.
- A declared class/fact constructor accepts named fields or positional values.
  Positional values map deterministically to the authoritative inherited-then-
  declared field order used by class construction; persistent fact seeds and
  transient constructors must share that same resolver.
- Integer-only fast paths (token-id scans, hashing, id comparison) may be written
  as header-only C-style kernels: `static inline` functions over integers and raw
  arrays in the C subset, kept `extern "C"`-compatible, in `src/IntKernels.h`.
  Memory management, containers, and data manipulation stay in C++. Do not add
  C source files or a C build language without a profile that shows a specific
  kernel gains from it.
