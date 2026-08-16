#pragma once

#include <cstddef>
#include <cstdint>
#include <bit>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Felidae {

struct IrModule;

using IrWord = std::size_t;
using RegisterId = std::size_t;
using IrConstantId = std::size_t;
using IrSymbolRef = std::size_t;
using IrFactRef = std::size_t;

enum class IrOpcode : IrWord {
    End = 0,
    LoadConst,
    LoadSymbol,
    StoreSymbol,
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
    // destination, model-operation symbol, input count, input registers.
    // This explicit recurrent/SSM operation keeps the binary operand schema
    // stable while using the typed RuntimeStateModel boundary.
    SsmProcess,
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

struct IrProcedure {
    FelidaeIr ir;
    std::vector<IrSymbolRef> positionalParameters;
    std::vector<IrSymbolRef> namedParameters;
    IrSourceMapEntry::Span sourceSpan;
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
    // VM text is runtime data. It deliberately has no tokenizer dependency.
    std::vector<std::size_t> pieces;
    std::string utf8;
    bool operator==(const VmText&) const = default;
};
struct VmArray;
using VmArrayPtr = std::shared_ptr<VmArray>;
struct VmMap;
using VmMapPtr = std::shared_ptr<VmMap>;
struct VmFact;
using VmFactPtr = std::shared_ptr<VmFact>;
using VmValue = std::variant<VmNil, bool, double, VmText, VmArrayPtr, VmMapPtr, VmFactPtr>;
// Public runtime spelling for canonical VM values.
using Value = VmValue;
// Typed result rendering for direct VM execution. It deliberately does not
// reconstruct AST nodes or consult Interpreter display helpers.
std::string vmValueToDisplayString(const VmValue& value);
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

// This object is created for a top-level VM execution. Nested procedure VMs
// receive the same execution state, while separate calls to RegisterVm never
// share it. A model state is opaque to the VM but lifetime-bound to this scope.
struct RuntimeContext {
    std::size_t maximumSemanticSteps = 1024;
    std::size_t semanticSteps = 0;
    std::shared_ptr<void> executionState;
    std::shared_ptr<std::size_t> sharedSemanticSteps;
};

class RuntimeStateModel {
public:
    virtual ~RuntimeStateModel() = default;
    // Called once for each top-level DirectVmRuntime execution. Backends use
    // this to allocate their recurrent/SSM state without retaining it across
    // requests. The default model is stateless.
    virtual std::shared_ptr<void> createExecutionState() { return {}; }
    virtual Value evaluate(const RuntimeOperation& operation,
                             std::span<const Value> inputs,
                             RuntimeContext& context) = 0;
};

class VmRuntime {
public:
    virtual ~VmRuntime() = default;
    virtual VmValue loadSymbol(IrSymbolRef symbol);
    virtual void storeSymbol(IrSymbolRef symbol, const VmValue& value);
    virtual VmValue callSymbol(IrSymbolRef symbol, std::span<const VmValue> arguments);
    virtual VmValue callSymbolNamed(IrSymbolRef symbol, std::span<const VmCallArgument> arguments);
    virtual VmValue callNativeSymbol(IrSymbolRef symbol);
    // Null means this execution has no learned semantic backend. Semantic IR
    // then fails in a controlled manner; exact opcodes never use this hook.
    virtual RuntimeStateModel* runtimeStateModel();
    virtual void beginExecution();
    virtual void endExecution() noexcept;
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

// Runtime used for closed, directly lowered programs.  Its sole purpose is
// to make the IR/VM boundary explicit: an instruction which needs legacy
// program services is rejected instead of implicitly reaching Interpreter.
class DirectVmRuntime final : public VmRuntime {
public:
    explicit DirectVmRuntime(std::unordered_map<IrSymbolRef, IrProcedure> procedures = {},
                             RuntimeStateModel* semanticModel = nullptr,
                             std::size_t maximumSemanticSteps = 1024,
                             std::size_t maximumCallDepth = 256);
    VmValue loadSymbol(IrSymbolRef symbol) override;
    void storeSymbol(IrSymbolRef symbol, const VmValue& value) override;
    VmValue callSymbol(IrSymbolRef symbol, std::span<const VmValue> arguments) override;
    VmValue callSymbolNamed(IrSymbolRef symbol, std::span<const VmCallArgument> arguments) override;
    RuntimeStateModel* runtimeStateModel() override;
    void beginExecution() override;
    void endExecution() noexcept override;
    RuntimeContext makeRuntimeContext(const FelidaeIr& ir,
                                      const VmValue& systemInput) const override;

private:
    // A frame has no AST payload and no shared registers. RegisterVm owns the
    // fresh register vector for every invocation; the runtime owns only the
    // procedure's lexical parameter/local bindings.
    struct VmCallFrame {
        IrSymbolRef procedure = 0;
        std::unordered_map<IrSymbolRef, VmValue> locals;
    };
    std::unordered_map<IrSymbolRef, VmValue> globals_;
    std::vector<VmCallFrame> callFrames_;
    std::unordered_map<IrSymbolRef, IrProcedure> procedures_;
    RuntimeStateModel* semanticModel_ = nullptr;
    std::size_t maximumSemanticSteps_ = 1024;
    std::size_t maximumCallDepth_ = 256;
    std::size_t procedureDepth_ = 0;
    std::size_t executionDepth_ = 0;
    std::shared_ptr<void> executionState_;
    std::shared_ptr<std::size_t> sharedSemanticSteps_;
};

class RegisterVm {
public:
    VmValue execute(const FelidaeIr& ir, VmRuntime& runtime, VmValue systemInput);
    // Canonical module entry point. The initializer owns global setup and
    // dispatches the declared entry procedure through ordinary verified IR.
    VmValue executeMain(const IrModule& module, VmRuntime& runtime,
                        VmValue systemInput = VmNil{});

private:
    std::vector<VmValue> registers_;
};

} // namespace Felidae
