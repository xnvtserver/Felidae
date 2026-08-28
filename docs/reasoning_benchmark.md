# Felidae reasoning and robustness benchmark

This benchmark does **not** assign an IQ number. IQ tests are calibrated for
people and would produce a misleading score for a compiler and virtual
machine. The benchmark instead reports concrete capabilities that can be
reproduced from source.

The initial domains are a
[coffee vending controller](../v2_examples/coffee_vending_theorem_solver.fx)
and an
[air-conditioner controller](../v2_examples/air_conditioner_theorem_solver.fx).
The expected cases are versioned in
[`felidae-reasoning-v1.jsonl`](../datasets/benchmarks/felidae-reasoning-v1.jsonl).

## What the examples prove

Both programs use hierarchical facts, explicit structural predicates,
numeric truth, theorem-like mixfix expressions, and ordered alternative
proofs. A failed candidate falls through to another proof, so the examples
exercise bounded backtracking. Invalid HVAC sensor evidence reaches a safe
lockout and missing coffee resources reach a refund instead of an unsafe
action.

This is deliberately narrower than Prolog. Felidae currently has no native
logic variables, occurs check, automatic unification, choice-point stack,
cut, or exhaustive search engine. The `requestUnifies` and
`observationUnifies` predicates are explicit domain unifiers, and the choice
procedures define a finite search order. Calling this full Prolog
backtracking would overstate the implementation.

## Reproducible scoring

Build commands can take time and must be run by the developer. Keep every
artifact below `build/debug`:

```bash
cmake --build build/debug --target felidae_compiler felidae_vm --parallel 1

./build/debug/felidae_compiler v2_examples/coffee_vending_theorem_solver.fx
./build/debug/felidae_vm build/debug/coffee_vending_theorem_solver.bin

./build/debug/felidae_compiler v2_examples/air_conditioner_theorem_solver.fx
./build/debug/felidae_vm build/debug/air_conditioner_theorem_solver.bin
```

The native benchmark target runs the same source-to-verified-IR path in one
process and emits one JSON result. Build it explicitly, then supply expected
action text from the versioned manifest:

```bash
cmake --build build/debug --target felidae_reasoning_benchmark --parallel 1

./build/debug/felidae_reasoning_benchmark --iterations 100 \
  v2_examples/coffee_vending_theorem_solver.fx \
  --expect dispense_coffee --expect refund

./build/debug/felidae_reasoning_benchmark --iterations 100 \
  v2_examples/air_conditioner_theorem_solver.fx \
  --expect cool --expect ventilate --expect fault_lockout
```

The JSON uses numeric `1.0` and `0.0` for determinism and expectation status.
The iteration limit is 10,000; no benchmark or training runs implicitly.

Record these metrics rather than one opaque score:

| Metric | Measurement |
|---|---|
| Proof accuracy | Expected actions matched / five manifest cases |
| Safe-failure rate | Safe fallback reached / two injected fault cases |
| Hierarchy accuracy | Correct `isA` results / hierarchy assertions |
| Determinism | Identical output over 100 repeated VM executions |
| Compile rejection | Invalid programs rejected with a diagnostic / invalid programs |
| Compile latency | Median and p95 compiler wall time after five warmups |
| VM latency | Median and p95 VM wall time after five warmups |
| Generalization | Held-out programs compiled and executed correctly without adding them to training |

Do not count a crash, hang, `nil`, or silent halt as a correct rejection. For
fault tolerance, add cases for invalid sensor truth, missing resources,
unknown fact fields, malformed source, corrupted FELBIR bytes, and recursion
depth exhaustion. The compiler or verifier must reject malformed artifacts;
the domain policy must return a documented safe action for valid but adverse
inputs.

## Learning and out-of-box evaluation

Felidae code supplies executable rules; merely reading a new `.fx` file is
not model training. The compiler SSM learns mixfix target selection, while
the runtime SSM learns operation selection. Neither model may invent new
language semantics or bypass executable IR verification.

Use three disjoint sets when evaluating learning:

1. **Train** contains examples used by `felidae_compiler --train` or
   `felidae_vm --train`.
2. **Validation** selects checkpoints and thresholds.
3. **Held-out transfer** contains new operator wording, hierarchy depth, fact
   ordering, and fault combinations never present in the first two sets.

Report out-of-box accuracy before training, held-out accuracy after training,
the difference, and regression accuracy on the original suite. Never train
on the five benchmark answers and then report those same rows as evidence of
generalization. Save the exact dataset hash, seed, model hash, build type,
CPU, and command line with every result.

## Current strengths and weaknesses

Felidae is stronger than a hand-written finite-state table when policies are
shared across many hierarchical fact types, explanations and proof scores
matter, or new combinations of known facts must be evaluated. Verified IR,
bounded indexes, deterministic lowering, and numeric truth also give a useful
safety boundary.

A conventional finite-state machine remains stronger for tiny controllers
that require certified hard real-time bounds, minimal memory, and an obvious
enumeration of every transition. Felidae's explicit finite alternatives can
grow combinatorially, its learned selectors require held-out validation, and
it does not yet provide a complete logical theorem prover. Physical coffee or
HVAC control additionally needs a trusted I/O layer, actuator interlocks,
timeouts, and independent safety limits outside the reasoning program.

## Fact-reasoning baseline and improvement gates

The August 2026 Debug smoke audit used the checked-in compiler and VM without
training. It is a capability baseline, not a trained-model score:

| Capability | Result | Evidence |
|---|---:|---|
| Numeric similarity and fuzzy membership | pass | `degree_profiles.fx` returned `Degree` values 0.125 and 0.696947396356321 |
| Degree preservation inside facts/maps | pass | similarity, membership, confidence, and truth degree remained separate values |
| Explicit threshold over a Degree | pass | 0.696947396356321 produced `not-met` for the 0.75 threshold |
| Temporal fact ranking | pass | 2025/priority-2 preceded both 2024/priority-1 facts |
| Bounded controller proofs | 5/5 actions | coffee dispense/refund and HVAC cool/ventilate/lockout matched |
| Deep fact analysis example | compile failure | unsupported typed-lambda lowering |
| Multi-ancestor comparison example | compile failure | unsupported `Relation.compare` lowering |
| Heterogeneous fact-expression result contract | pass | crisp degree, probability, recoverable error, derived fact, and evidence-list results executed through FELBIR |

This sample passes the two controller scenarios and the heterogeneous
fact-expression contract. Two selected rich fact-comparison scenarios remain
compiler-rejected. Do not turn that small sample into a general intelligence
percentage: it is not balanced by hierarchy depth, data size, negative
evidence, or held-out domains.

The deterministic VM primitives are stronger than the source-level feature
surface currently reaching them. Hierarchy proofs, common ancestors, least and
most-general common ancestors, structural similarity, Gaussian membership,
and temporal ranking have verified executable-IR implementations. Several
documented high-level comparison and fact-query forms still fail during IR
generation.

### Measured engineering bottlenecks

- Map and fact similarity performs nested field scans, giving quadratic field
  comparison cost. Canonical field indexes would make matching near-linear.
- Fact similarity gives 25% weight to exact type identity and does not use
  hierarchy distance. Related subtypes can score worse than unrelated facts
  with coincidentally similar fields.
- Numeric similarity uses `1 / (1 + absolute_difference)`. It is deterministic
  but unit-sensitive across temperature, salary, probability, and other scales.
- Least/most-general ancestor calculation launches repeated hierarchy searches
  for pairs of common ancestors. Dense multiple-inheritance graphs can require
  quadratic pair checks, each with another graph traversal.
- Temporal ranking scans every retained fact and fails if any fact lacks the
  requested fields. It needs a type- or query-bounded indexed candidate set.
- Benchmark `--expect` checks display substrings. It cannot prove exact fact
  identity, field differences, calibrated Degrees, or proof paths.
- Felidae has bounded explicit alternatives, but no native logic variables,
  occurs check, choice-point stack, or complete unification engine.

### Intelligence improvement plan

1. **Complete the source-to-IR surface.** Compile typed fact iteration,
   relationship comparison, and maintained fact-analysis examples. Add a
   compile-and-run regression for every supported public construct.
2. **Make benchmarks typed.** Assert exact result paths, value kinds, fact
   types, ordered IDs, Degree values with tolerances, and expected failures.
   Keep substring checks only as presentation smoke tests.
3. **Improve deterministic comparison first.** Canonicalize/index fields, add
   schema-declared numeric scales and weights, incorporate hierarchy distance,
   and return evidence with contributions, missing fields, paths, and provenance.
4. **Improve graph algorithms.** Cache revision-keyed ancestor closures and
   compute minimal/maximal common ancestors without nested full searches. Test
   chains, diamonds, multiple inheritance, cycles, and disconnected graphs.
5. **Add bounded unification deliberately.** Introduce typed variables,
   substitutions, occurs check, deterministic choice points, step/depth/result
   limits, and proof traces as a verified runtime service.
6. **Calibrate non-Boolean reasoning.** Test identity, bounds, promised
   symmetry, monotonicity, normalized units, missing/conflicting evidence, and
   threshold sensitivity. Report calibration error for learned Degrees.
7. **Evaluate learning last.** Split by domain and hierarchy family, compare
   learned proposals with deterministic oracle results, require abstention on
   uncertainty, and report accuracy, invalid proposals, calibration,
   median/p95 latency, and memory independently.

The beta gate should require 100% deterministic primitive and safety-case
accuracy, zero invalid values entering VM registers, zero silent halts, stable
results across 100 runs, and separately reported held-out learned accuracy.

## SSM utilization audit

The checked-in compiler corpus currently contains 184 `ACCEPT`, 12 `ABSTAIN`,
and 22 `REJECT` decisions. The compiler SSM is used only when verified parser
context leaves a mixfix target ambiguous; exact syntax, numeric intrinsics,
tensor operations, and ordinary IR generation remain deterministic. This is
the intended boundary: adding tensor examples to compiler-SSM training would
teach no ambiguity and would only duplicate the compiler.

The VM corpus currently contains 15 records, all for
`SemanticOperationId::Identity`. No runtime SSM artifact is shipped. The VM
SSM can return only a bounded input/fact reference, `nil`, numeric truth
`0.0`/`1.0`, or one of five Degree values. This gives a safe extension point
and recurrent request-local state, but the present corpus does not demonstrate
dynamic fact selection, derivation, hierarchy-sensitive decisions, calibrated
soft evidence, or held-out generalization.

Before claiming those capabilities, add independently labelled records for
`SelectFact`, `DeriveFact`, and `EvaluateDegree`; balance fact kinds,
populations, hierarchy shapes, and negative/abstention cases; split by domain
and hierarchy family; then report exact action accuracy, invalid-output rate,
Degree calibration, abstention quality, and p50/p95 latency. Deterministic
tensor comparison stays outside this model and supplies auditable features or
oracle labels rather than learned arithmetic.
