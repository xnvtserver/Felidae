# Felidae executable IR

Felidae has one executable instruction representation. The compiler emits it,
the FELBIR writer serializes it, and the Form register VM dispatches it
directly. There is no assembler, fixed-width ISA, or intermediate lowering
format.

## Encoding contract

FELBIR v16 stores 32-bit integer words in little-endian order. An instruction
starts with an `IrOpcode`; its opcode determines a fixed or bounded
variable-width operand sequence. Calls, named calls, arrays, maps, `Builtin`,
`Numeric`, and `SemanticEval` carry explicit counts, and the verifier checks
those bounds before execution.

Registers, constants, procedures, source spans, and symbol references use
bounded integer indexes. A one-based symbol reference addresses exactly one
entry in the module symbol table. Each entry is the complete SentencePiece ID
sequence for that symbol. Hashes, UTF-8 spellings, and process-global symbol
registries are not executable identity.

The beta format has no compatibility reader. A change to the executable
contract increments `kBinaryIrVersion`, and existing `.bin` files must be
rebuilt.

## Verification boundary

The compiler verifies a completed module before writing it. The binary loader
treats bytes as hostile, checks the FELBIR header and exact SentencePiece model
identity, constructs a module, verifies it once, and returns
`VerifiedIrModule`. The writer and VM accept only that typestate, so they do
not repeat whole-module verification.

Verification covers instruction widths and operands, register and pool
indexes, control flow, initialized-register use, procedure calls and metadata,
symbol references, fact hierarchies, and semantic-operation contracts.

## Runtime truth

The VM has no Boolean value alternative. Crisp truth is always a `double`:
`0.0` is false and `1.0` is true. Boolean constants, comparisons, and logical
operations produce only those values. Conditional control accepts `nil`,
`0.0`, or `1.0` and rejects other values instead of inventing truthiness.

## Runtime library operations

`Builtin` contains a stable operation ID and an ordered input-register list.
The compiler, verifier, and VM share one arity contract. The VM calls the
corresponding typed C++ implementation directly; there is no native-library
loader, string dispatch, second interpreter, or SSM involvement. JSON and CSV
text boundaries use the module's SentencePiece encoder and decoder. A runtime
without either required service fails explicitly instead of returning a
placeholder value.

## Numeric operations

The variable-width `Numeric` instruction contains a destination register, a
`NumericOperation`, and its bounded input-register list. `RegisterVm` executes
MIN, MAX, ABS, DIFF, AVG, WEIGHTED_AVG, CLAMP, FLOOR, CEIL, ROUND, TRUNC,
SQRT, CBRT, POW, EXP, LOG, LOG10, LERP, SIGN, RECIPROCAL, SQUARE, CUBE,
IN_RANGE, IS_FINITE, and IS_NAN directly. The callable `MOD` form reuses the
existing `Mod` opcode and `std::fmod` implementation.

## Tensor operations

The variable-width `Tensor` instruction uses the same destination, operation,
arity, and input-register layout as `Numeric`. On supported builds its one
backend is LibTorch. Numeric arrays become `float64` tensors; text, symbols,
and facts become one-dimensional feature tensors. A fact tensor contains the
complete SentencePiece sequences for its type, ordered field names, text and
symbol values, plus typed numeric and Degree field values. Numeric values are
preserved as numbers rather than reinterpreted as token IDs.

`size`, `shape`, `dimensions`, `clone`, `transpose`, `isSymmetric`, absolute
`difference`, cosine similarity, dot product, mean-squared error, sigmoid, and
ReLU execute directly at this boundary. Non-scalar results remain real
LibTorch tensors in `VmValue`; they are materialized as nested arrays only by
the display adapter or another explicit boundary. `clone` performs an actual
tensor clone. `isSymmetric` returns only `0.0` or `1.0`.
Ragged arrays, incompatible shapes, invalid ranks, non-finite numeric inputs,
and cosine similarity with a zero vector fail explicitly. These deterministic
operations never invoke the runtime SSM.

Ordinary numeric operations require finite numeric operands and a finite
result. Invalid ranges, zero reciprocal or total weight, negative square-root
inputs, non-positive logarithm inputs, and overflow/domain failures raise an
`IrError`. IS_FINITE and IS_NAN deliberately inspect non-finite inputs and
return numeric `0.0` or `1.0`; they do not introduce Boolean VM values. No
ordinary numeric operation invokes an SSM.

## Semantic operations

`SemanticEval` carries a stable `SemanticOperationId` and an ordered list of
typed input registers. The runtime SSM selects one bounded typed result; it
cannot generate instructions. Its knowledge context resolves module-local
symbol references through the same module symbol table and supplies complete
SentencePiece ID sequences to inference and training.
