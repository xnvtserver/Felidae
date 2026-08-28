<!-- omit in toc -->

# Contributing to Felidae

Thank you for your interest in contributing to **Felidae**. ❤️

Felidae is an open-source C++ language, compiler, reasoning runtime, and register-based virtual machine under active development. Contributions involving the compiler, VM, reasoning system, documentation, tests, tooling, language integrations, editor support, and related components are welcome.

Felidae is currently in **beta**, so correctness, stability, test coverage, clear documentation, and backward-compatible improvements are especially valuable.

Before contributing, please read the relevant sections below and review the existing documentation, issues, and project structure.

If you would like to support Felidae without contributing code, you can also:

* ⭐ Star the repository
* Share Felidae with developers and researchers
* Reference Felidae in related projects or articles
* Improve documentation and examples
* Report reproducible bugs
* Participate in technical discussions
* Test beta releases and provide feedback

<!-- omit in toc -->

## Table of Contents

* [Code of Conduct](#code-of-conduct)
* [Questions and Support](#questions-and-support)
* [Contributing](#contributing)

  * [Legal Notice](#legal-notice)
  * [Reporting Bugs](#reporting-bugs)
  * [Reporting Security Vulnerabilities](#reporting-security-vulnerabilities)
  * [Suggesting Enhancements](#suggesting-enhancements)
  * [Your First Code Contribution](#your-first-code-contribution)
  * [Working With Dependencies](#working-with-dependencies)
  * [Improving Documentation](#improving-documentation)
* [Development Guidelines](#development-guidelines)

  * [C++ Changes](#c-changes)
  * [Compiler Changes](#compiler-changes)
  * [VM Changes](#vm-changes)
  * [Tests](#tests)
  * [Commit Messages](#commit-messages)
* [Pull Requests](#pull-requests)
* [Editor and Tooling Contributions](#editor-and-tooling-contributions)
* [Beta Development Policy](#beta-development-policy)
* [Joining the Project](#joining-the-project)
* [Attribution](#attribution)

## Code of Conduct

Felidae and everyone participating in the project are governed by the [Felidae Code of Conduct](https://github.com/xnvtserver/Felidae/blob/main/CODE_OF_CONDUCT.md).

By participating in the project, you agree to uphold the Code of Conduct.

Unacceptable behavior may be reported privately to:

**[info@xnovity.com](mailto:info@xnovity.com)**

## Questions and Support

Before opening a new issue, please review the available [Felidae documentation](https://github.com/xnvtserver/Felidae/tree/main/docs).

Also search the existing [GitHub Issues](https://github.com/xnvtserver/Felidae/issues) to determine whether your question or problem has already been discussed.

If you still need help:

1. Open a [GitHub Issue](https://github.com/xnvtserver/Felidae/issues/new).
2. Clearly describe what you are trying to accomplish.
3. Include the Felidae version or commit you are using.
4. Include your operating system and relevant development environment information.
5. Include a minimal example when applicable.

Please keep questions focused and provide enough information for another contributor to understand the problem.

## Contributing

Contributions may include:

* Compiler improvements
* Integer parser improvements
* IR validation and optimization
* Register VM improvements
* Fact and reasoning functionality
* Fuzzy and degree-based operations
* State-model and semantic evaluation improvements
* Performance optimizations
* Cross-platform improvements
* Tests and regression cases
* Documentation
* Examples
* Build-system improvements
* Editor integrations
* Tree-sitter and syntax tooling
* Bug fixes
* Security improvements

Large architectural changes should normally be discussed through an issue before substantial implementation work begins.

### Legal Notice

By submitting a contribution, you confirm that:

* You created the contribution or otherwise have the legal right to submit it.
* You have the necessary rights to the contributed content.
* Your contribution may be distributed under the license used by the Felidae project.
* Your contribution does not knowingly introduce code or content with incompatible licensing requirements.

Do not submit proprietary source code, confidential information, credentials, private keys, production data, or other material that you are not authorized to distribute.

### Reporting Bugs

#### Before Submitting a Bug Report

Before opening a bug report:

* Make sure you are testing against a currently supported Felidae version.
* Review the documentation.
* Search existing issues for the same or a similar problem.
* Verify that the problem is not caused by an unsupported compiler, incompatible dependency, incorrect build configuration, or local environment issue.
* Try to reduce the problem to the smallest reproducible example.

Useful information includes:

* Felidae version or Git commit
* Operating system
* Architecture, such as `x86_64` or `ARM64`
* C++ compiler and version
* CMake version
* Build type, such as `Debug` or `Release`
* Relevant CMake options
* Input `.fx` source, when applicable
* Compiler output
* VM output
* Expected behavior
* Actual behavior
* Stack trace or crash information
* Minimal reproduction steps

If the problem concerns generated IR or VM execution, include the smallest input necessary to reproduce the problem whenever possible.

#### Submitting a Bug Report

Use the [Felidae issue tracker](https://github.com/xnvtserver/Felidae/issues) for normal bugs.

A useful bug report should explain:

1. What you attempted to do
2. What you expected to happen
3. What actually happened
4. How another contributor can reproduce it
5. Which Felidae version or commit is affected

Please avoid posting unnecessarily large logs. Reduce logs and examples to the portions relevant to the issue whenever possible.

Maintainers may apply labels such as `bug`, `needs-repro`, `compiler`, `vm`, `documentation`, or other appropriate classifications.

Issues that cannot be reproduced may require additional information before work can continue.

### Reporting Security Vulnerabilities

**Do not report security vulnerabilities through public GitHub issues.**

Security vulnerabilities and reports containing sensitive security information should be submitted privately according to the project's [Security Policy](https://github.com/xnvtserver/Felidae/blob/main/SECURITY.md).

You may also contact:

**[info@xnovity.com](mailto:info@xnovity.com)**

or

**[support@xnovity.com](mailto:support@xnovity.com)**

Please allow maintainers reasonable time to investigate and address a vulnerability before publicly disclosing technical details.

### Suggesting Enhancements

Feature requests and enhancement proposals are welcome.

Before creating one:

* Review the documentation.
* Search existing issues.
* Check whether the functionality already exists.
* Consider whether the proposal fits Felidae's architecture and project goals.

Create an issue describing:

* The problem you are trying to solve
* The proposed behavior
* Why the change is useful
* Possible implementation considerations
* Compatibility implications
* Alternatives you considered

For language, compiler, IR, VM, storage, reasoning, or model-related architectural changes, explain how the proposal interacts with existing components.

Avoid combining several unrelated architectural changes into a single proposal.

### Your First Code Contribution

Start by cloning Felidae with its submodules:

```bash
git clone --recurse-submodules https://github.com/xnvtserver/Felidae.git
cd Felidae
```

If you already cloned the repository without its submodules:

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

Create a development branch:

```bash
git checkout -b fix/short-description
```

or:

```bash
git checkout -b feature/short-description
```

Configure a development build using CMake:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFELIDAE_BUILD_TESTS=ON
```

Build:

```bash
cmake --build build
```

Run the available tests:

```bash
ctest --test-dir build --output-on-failure
```

Build options and dependencies may evolve during beta development. Check the repository documentation and `CMakeLists.txt` for the currently supported configuration.

Before opening a pull request, make sure the project builds successfully and relevant tests pass.

### Working With Dependencies

Felidae uses Git submodules for several external dependencies and project components.

Do not casually update submodule revisions as part of an unrelated change.

Dependency updates should normally be isolated so that compatibility and regression testing can be performed independently.

After changing or updating submodules, verify the repository from a clean state:

```bash
git submodule sync --recursive
git submodule update --init --recursive
git submodule status --recursive
```

Do not modify third-party source code directly unless the change is intentional, documented, and necessary.

Avoid introducing new dependencies when equivalent functionality can reasonably be implemented using the C++ standard library or an existing Felidae dependency.

New dependencies should have:

* A clear technical justification
* A compatible license
* Active maintenance
* Acceptable build and runtime overhead
* Cross-platform support where applicable

### Improving Documentation

Documentation contributions are welcome and do not require changes to the compiler or runtime.

Useful documentation contributions include:

* Correcting outdated information
* Improving explanations
* Adding examples
* Documenting language behavior
* Documenting compiler behavior
* Documenting VM semantics
* Improving build instructions
* Improving platform-specific setup instructions
* Documenting APIs and tooling

Documentation should describe behavior that actually exists in the repository. Clearly identify experimental or planned functionality rather than documenting it as stable behavior.

## Development Guidelines

Felidae contains components with different responsibilities. Contributions should preserve boundaries between these components rather than bypassing them for convenience.

### C++ Changes

When contributing C++ code:

* Prefer clear and maintainable C++.
* Avoid unnecessary allocations and copies in performance-sensitive paths.
* Use RAII for resource ownership.
* Prefer explicit ownership and lifetime semantics.
* Avoid introducing global mutable state without strong justification.
* Avoid undefined behavior.
* Keep warnings clean where practical.
* Preserve cross-platform compatibility.
* Add tests for behavioral changes and bug fixes.

Performance optimizations should preserve correctness and should ideally include measurements when the performance impact is significant.

### Compiler Changes

Compiler changes should preserve the separation between source-language processing and VM execution.

When changing parsing, compilation, or IR generation:

* Keep source-level concerns within the compiler/frontend.
* Validate generated IR before execution.
* Reject unsupported constructs clearly rather than silently changing their meaning.
* Preserve useful source-location information for diagnostics where applicable.
* Add regression tests for compiler bugs.

Do not introduce runtime dependencies on compiler AST structures merely to simplify implementation.

### VM Changes

The Felidae VM executes compiled IR and should remain independent of source-language parsing details.

VM changes should:

* Preserve the defined IR contract.
* Validate operands and runtime state where appropriate.
* Avoid dependencies on parser or AST implementation details.
* Keep deterministic behavior deterministic.
* Clearly isolate learned, soft, fuzzy, or non-deterministic semantic behavior from deterministic VM operations.
* Include tests for new instructions and semantic operations.

Changes to the VM instruction set or IR representation should be treated as architectural changes and discussed before large implementations are submitted.

### Tests

Bug fixes should include regression tests whenever practical.

New functionality should include tests covering:

* Expected behavior
* Important edge cases
* Invalid inputs where applicable
* Compiler/IR boundaries where relevant
* VM behavior where relevant

Run:

```bash
ctest --test-dir build --output-on-failure
```

before submitting a pull request.

A pull request should not intentionally disable an existing test merely to make the test suite pass.

### Commit Messages

Use concise commit messages that describe the change.

Recommended prefixes include:

```text
feat: add ...
fix: correct ...
docs: update ...
test: add ...
refactor: simplify ...
perf: optimize ...
build: update ...
ci: configure ...
deps: update ...
```

Examples:

```text
fix: validate register operands before execution
```

```text
feat: add degree minimum operation
```

```text
test: add VM semantic evaluation cases
```

```text
docs: clarify compiler and VM boundaries
```

Keep unrelated changes in separate commits where practical.

## Pull Requests

Before opening a pull request:

* Rebase or update your branch against the current target branch when necessary.
* Build Felidae successfully.
* Run relevant tests.
* Review your own diff.
* Remove temporary debugging code.
* Remove generated build artifacts.
* Avoid unrelated formatting changes.
* Document user-visible or architectural changes.

A good pull request should contain:

* A clear title
* A concise description of the problem
* A summary of the implementation
* Testing performed
* Relevant issue references
* Compatibility or migration considerations, when applicable

Small, focused pull requests are generally easier to review than large changes combining unrelated work.

Maintainers may request changes before merging.

Submission of a pull request does not guarantee that the change will be accepted.

## Editor and Tooling Contributions

Felidae may include editor integrations, syntax definitions, Tree-sitter components, and other development tooling.

Changes to these components should remain synchronized with the currently supported Felidae syntax and behavior.

When changing language syntax, consider whether corresponding changes are required for:

* Tree-sitter grammar
* VS Code support
* Vim support
* Emacs support
* IntelliJ IDEA support
* Sublime Text support
* Zed support
* Notepad++ support
* Nano syntax support
* Documentation and examples

Tooling changes should normally be made in the appropriate component or submodule rather than duplicating editor-specific logic inside the compiler.

## Beta Development Policy

Felidae is currently in beta.

During beta development:

* APIs may evolve.
* IR formats may evolve.
* VM instructions may evolve.
* Language syntax may evolve.
* Model formats and training procedures may evolve.
* Compatibility between beta releases is not guaranteed unless explicitly documented.

However, changes should still be deliberate and reviewed.

Do not use beta status as justification for unnecessary breaking changes. Prefer migration paths and compatibility where they are practical.

Correctness, architecture, maintainability, testing, and security take priority over preserving experimental behavior that is known to be incorrect.

## Joining the Project

Regular contributors who consistently provide useful code, documentation, testing, reviews, or technical guidance may become more involved with the project over time.

Project roles and repository permissions are granted at the discretion of the maintainers based on contribution history, project needs, and demonstrated understanding of the project's technical and community standards.

The best way to become involved is to participate constructively through issues, discussions, reviews, documentation, and focused pull requests.

Thank you for helping build Felidae. ❤️

<!-- omit in toc -->

## Attribution

Parts of the original contribution guidelines were based on the [contributing.md generator](https://contributing.md/).

This document has been adapted for the Felidae project and its development workflow.
