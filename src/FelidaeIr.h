#pragma once

#include <cstddef>
#include <cstdint>
#include <bit>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace Felidae {

using SentencePieceId = std::size_t;
using IrWord = std::size_t;
using RegisterId = std::size_t;
using IrConstantId = std::size_t;
using IrSymbolRef = std::size_t;
using IrFactRef = std::size_t;

enum class IrOpcode : IrWord {
    End = 0,
    LoadConst,
    LoadSymbol,
    Move,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Compare,
    Jump,
    JumpIfFalse,
    Call,
    CallNamed,
    CallNative,
    SemanticEval,
    MakeFact,
    MakeArray,
    MakeMap,
    GetField,
    SetField,
    Return,
    ExecuteProgram,
    Count
};

constexpr IrWord kIrOpcodeCount = static_cast<IrWord>(IrOpcode::Count);

enum class IrOperandKind : IrWord { Register = 1, Constant, Symbol, Fact, Jump, Program };
enum class IrConstantKind : std::uint8_t { Number, Boolean, Nil, Text };
enum class IrComparison : IrWord { Equal = 0, NotEqual, Less, LessEqual, Greater, GreaterEqual };

struct IrSourceMapEntry {
    std::size_t instructionWord = 0;
    struct Span { int startLine = 1; int startColumn = 1; int endLine = 1; int endColumn = 1; } sourceSpan;
};

struct FelidaeIr {
    std::vector<IrWord> words;
    std::vector<IrWord> constants;
    std::vector<IrConstantKind> constantKinds;
    // Text is a dedicated side table: integer IR words carry only its bounded
    // index, never a pointer or unrestricted string payload.
    std::vector<std::string> texts;
    std::vector<IrWord> symbols;
    std::vector<IrWord> programs;
    std::vector<IrSourceMapEntry> sourceMap;
    std::size_t registerCount = 0;
};

class IrError : public std::runtime_error {
public:
    explicit IrError(const std::string& message) : std::runtime_error(message) {}
};

class IrVerifier {
public:
    static void verify(const FelidaeIr& ir);
};

// Constants are stored as machine words in canonical IR.  Numeric constants
// use these helpers rather than host string parsing or pointer payloads.
IrWord encodeIrNumber(double value) noexcept;
double decodeIrNumber(IrWord word) noexcept;

struct VmNil { bool operator==(const VmNil&) const = default; };
struct VmText {
    std::vector<SentencePieceId> pieces;
    std::string utf8;
    bool operator==(const VmText&) const = default;
};
struct VmArray;
using VmArrayPtr = std::shared_ptr<VmArray>;
struct VmMap;
using VmMapPtr = std::shared_ptr<VmMap>;
struct VmFact;
using VmFactPtr = std::shared_ptr<VmFact>;
struct VmOpaqueValue {
    std::shared_ptr<void> object;
    bool operator==(const VmOpaqueValue&) const = default;
};
using VmValue = std::variant<VmNil, bool, double, VmText, VmArrayPtr, VmMapPtr, VmFactPtr, VmOpaqueValue>;
// Public runtime spelling. VmValue remains the internal compatibility name
// used by the existing register VM and legacy bridge.
using Value = VmValue;
struct VmArray {
    std::vector<VmValue> values;
};
struct VmMap {
    std::vector<std::pair<IrSymbolRef, VmValue>> entries;
};
// Facts retain a separate type tag.  They are deliberately not aliases for
// maps: map-shaped values must not acquire fact semantics merely by carrying
// a similarly named field.
struct VmFact {
    IrSymbolRef type = 0;
    std::vector<std::pair<IrSymbolRef, VmValue>> fields;
};
struct VmCallArgument {
    // Empty means positional. A non-empty value is a parser-owned symbol
    // reference from the IR symbol table.
    std::optional<IrSymbolRef> name;
    VmValue value;
};

// A semantic operation is deliberately a symbol-table reference plus typed
// VM values. It never serializes facts or other runtime data back through
// text/SentencePiece merely to invoke a learned runtime backend.
struct RuntimeOperation {
    IrSymbolRef symbol = 0;
};

// This object is created afresh by RegisterVm::execute. A state model may
// store execution-local recurrent state in it, but cannot accidentally carry
// state between unrelated programs, requests, or VM invocations.
struct RuntimeContext {
    std::size_t maximumSemanticSteps = 1024;
    std::size_t semanticSteps = 0;
    std::shared_ptr<void> executionState;
};

class RuntimeStateModel {
public:
    virtual ~RuntimeStateModel() = default;
    virtual Value evaluate(const RuntimeOperation& operation,
                             std::span<const Value> inputs,
                             RuntimeContext& context) = 0;
};

class VmRuntime {
public:
    virtual ~VmRuntime() = default;
    virtual VmValue executeProgram(IrWord programRef, const VmValue& systemInput) = 0;
    virtual VmValue loadSymbol(IrSymbolRef symbol);
    virtual VmValue callSymbol(IrSymbolRef symbol, std::span<const VmValue> arguments);
    virtual VmValue callSymbolNamed(IrSymbolRef symbol, std::span<const VmCallArgument> arguments);
    virtual VmValue callNativeSymbol(IrSymbolRef symbol);
    // Null means this execution has no learned semantic backend. Semantic IR
    // then fails in a controlled manner; exact opcodes never use this hook.
    virtual RuntimeStateModel* runtimeStateModel();
    virtual RuntimeContext makeRuntimeContext(const FelidaeIr& ir,
                                              const VmValue& systemInput) const;
    // Runtime-owned handles (facts, native objects, hierarchy cursors) can be
    // checked here before a learned result enters a VM register. The default
    // admits only structurally valid VM values.
    virtual bool validateSemanticResult(const VmValue& value,
                                        const RuntimeContext& context) const;
    // Branch protocol, not generic truthiness.  The VM never coerces facts,
    // numbers, text, arrays, or opaque runtime values into booleans.  The
    // default branches only for explicit nil/false; a runtime may implement
    // its own control-value protocol without changing VM value support.
    virtual bool shouldBranchFalse(const VmValue& value) const;
};

class RegisterVm {
public:
    VmValue execute(const FelidaeIr& ir, VmRuntime& runtime, VmValue systemInput);

private:
    std::vector<VmValue> registers_;
};

} // namespace Felidae
