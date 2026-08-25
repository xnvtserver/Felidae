# Felidae ISA v1

Felidae ISA is the only executable instruction language accepted by the Form
VM. Compiler IR is an internal, structured lowering representation and is
never serialized or dispatched by the VM.

## Word encoding

Every instruction word is exactly 32 bits and little-endian in FELBIN. The
low byte is the permanent opcode. The remaining 24 bits use one of three
formats:

| Format | Bits 31–24 | Bits 23–16 | Bits 15–8 | Bits 7–0 |
| --- | --- | --- | --- | --- |
| `ABC` | C | B | A | opcode |
| `ABx` | Bx high | Bx low | A | opcode |
| `Ax` | Ax high | Ax middle | Ax low | opcode |

Registers are unsigned 8-bit indexes, so an ISA v1 frame has at most 256
registers. Constants and symbols use bounded pool indexes. Large values never
appear inline in executable words. Calls and other instructions with typed
argument lists use a fixed header followed by a verifier-bounded number of
32-bit extension words.

## Permanent opcode assignments

| Group | Opcode | Value |
| --- | --- | ---: |
| control | `Halt` | `0x00` |
| load/move | `LoadConstant` … `Move` | `0x10` … `0x13` |
| arithmetic | `Add` … `Modulo` | `0x20` … `0x24` |
| comparison/boolean | `CompareEqual` … `BooleanOr` | `0x30` … `0x38` |
| branches | `Jump`, `JumpIfFalse` | `0x40`, `0x41` |
| calls | `Call`, `CallNative`, `Return`, `CallNamed` | `0x50` … `0x53` |
| facts/values | `MakeFact` … `Membership` | `0x60` … `0x67` |
| hierarchy | hierarchy operations | `0x70` … `0x73` |
| temporal | `TemporalRank` | `0x78` |
| semantic | `SemanticEval` | `0x80` |

Values are never renumbered or reused within ISA v1. An incompatible change
requires a new ISA version.

## Runtime truth values

ISA comparisons and boolean operations produce numeric doubles only: `0.0`
is false and `1.0` is true. `JumpIfFalse` accepts only nil, `0.0`, or `1.0`;
it rejects every other number and does not invent truthiness for text, facts,
symbols, arrays, or maps. There is no boolean alternative in `VmValue`.

## Hierarchy and temporal operations

Hierarchy operands are typed symbol values or facts, in which case the fact's
type symbol is used. `HierarchyIsA` returns numeric `0.0` or `1.0`. Common,
least-common, and most-general ancestor operations return arrays of symbol
values. The runtime resolves all four operations through its fact hierarchy;
there is no hidden frontend or interpreter fallback.

`TemporalRank` names an effective-at field and a priority field through two
verified symbol-pool indexes. It returns the runtime's fact snapshot ordered
by descending effective time, then descending priority, with stable fact ID
as the deterministic tie-breaker.

The deterministic frontend lowers `isA(left: ..., right: ...)`,
`commonAncestors`, `lowestCommonAncestor`, and `highestCommonAncestor` to the
fixed hierarchy opcodes. It lowers
`temporalRank(effectiveAt: effective_at, priority: priority)` to
`TemporalRank`; field names become verified symbol-pool references rather than
runtime text or SentencePiece IDs.

A fact declaration designation such as `Animal(...) as animals` is resolved
by the compiler when lowering `for_each_fact(animals, callback)`. The emitted
`QueryFacts` still references the declared `Animal` type, so runtime selection
includes `Animal` and every registered descendant. The designation is not
serialized as a fact field and does not create a synthetic hierarchy edge.

The core `then` pipeline is also frontend-only syntax. The compiler evaluates
its left expression, binds that typed register as `system.result` only while
lowering the right expression, and emits ordinary calls/moves. Chained and
multiline pipelines remain deterministic; `system.result` outside the right
side of `then` is a compile error. A literal `then` inside a declared mixfix
pattern remains that pattern's anchor and is not rewritten as the core
pipeline operator.

## Semantic operations

`SemanticEval` contains a fixed `SemanticOperationId` and a typed register
input list. The runtime SSM returns one typed VM value, which is validated
before register assignment. It cannot generate instructions or binary words.
The v1 IDs are `Identity` (`0x0001`), `SelectFact` (`0x0002`), `DeriveFact`
(`0x0003`), and `EvaluateDegree` (`0x0004`).

All four v1 operations are unary. `Identity` accepts any structurally valid VM
value and must return the same value kind; `SelectFact` and `DeriveFact`
accept a fact and may return a fact or `nil`; `EvaluateDegree` accepts a number
or Degree and must return a bounded Degree. The IR verifier, assembler, ISA
verifier, runtime-model input boundary, output boundary, and JSONL-v7 teacher
loader enforce the applicable parts of this contract. Runtime training rows
carry the fixed 16-bit `operation_id`, never a hashed source symbol.

## Verification boundary

The compiler verifies structured IR before deterministic ISA lowering. The
ISA verifier then scans instruction boundaries and rejects unknown opcodes,
invalid registers, invalid constant/symbol/procedure indexes, invalid branch
targets, incomplete instructions, nonzero reserved bits, and unknown semantic
operation IDs. The VM must never dispatch an unverified block.

Canonical encoding is mandatory. Unused primary-word fields, high halves of
16-bit extension values, and unused lanes in the final packed-register word
must be zero. This prevents two different byte sequences from representing
the same verified instruction.

Verification also performs control-flow dataflow analysis. Every reachable
register read must be definitely initialized on all incoming paths, and a
reachable path may not fall through the end of an instruction block.

Verified loops remain legal, but execution is resource-bounded. `RegisterVm`
uses one instruction-step budget across the initializer and all nested calls
(10,000,000 instructions by default); exceeding it is a controlled VM error.
Hosts may supply a smaller positive budget. Structural value traversal,
procedure depth, semantic steps, and trace retention are bounded separately so
cyclic or adversarial values cannot hang the process.

The compiler's learned structural decision is explicit: `ACCEPT` permits the
already-verified compiler IR to proceed to deterministic lowering;
`REJECT` and `ABSTAIN` stop compilation. Model output can never become an
opcode or bypass either verifier.

SentencePiece token IDs and source offsets are frontend data. Text crosses the
compiler boundary only as UTF-8 constant-pool values; tokenizer IDs are never
opcodes, operands, or runtime semantics.

## FELBIN display metadata

FELBIN v10 carries a sorted symbol-ID-to-UTF-8-name table for clean result
rendering in the separate VM process. This table is non-executable metadata:
instructions still reference bounded symbol-pool indexes, the verifier checks
the table's canonical ordering, and the VM never sends names through
SentencePiece or treats them as opcodes. If a handcrafted ISA module omits a
name, rendering falls back unambiguously to `#<symbol-id>`.
