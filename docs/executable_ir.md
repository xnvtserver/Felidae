# Felidae executable IR

Felidae has one executable instruction representation. The compiler emits it,
the FELBIR writer serializes it, and the Form register VM dispatches it
directly. There is no assembler, fixed-width ISA, or intermediate lowering
format.

## Encoding contract

FELBIR v13 stores 32-bit integer words in little-endian order. An instruction
starts with an `IrOpcode`; its opcode determines a fixed or bounded
variable-width operand sequence. Calls, named calls, arrays, maps, and
`SemanticEval` carry explicit counts, and the verifier checks those bounds
before execution.

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

## Semantic operations

`SemanticEval` carries a stable `SemanticOperationId` and an ordered list of
typed input registers. The runtime SSM selects one bounded typed result; it
cannot generate instructions. Its knowledge context resolves module-local
symbol references through the same module symbol table and supplies complete
SentencePiece ID sequences to inference and training.
