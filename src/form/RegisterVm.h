#pragma once

#include <cstddef>
#include <cstdint>
#include <bit>
#include <functional>
#include <memory>
#include <mutex>
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
    // Deterministic fuzzy primitives. They produce VmDegree, never bool.
    Similarity,
    // destination, value, peak, fades-in, fades-out registers.
    Membership,
    // destination, fact-type symbol, deterministic callback symbol.
    ForEachFact,
    Return,
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
    // Every text constant is a SentencePiece ID sequence. Raw UTF-8 never
    // enters .fir or the VM's persistent state.
    std::vector<std::vector<std::uint32_t>> texts;
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
struct VmDegree {
    double value = 0.0;
    explicit VmDegree(double value);
    bool operator==(const VmDegree&) const = default;
};
struct VmText {
    std::vector<std::uint32_t> pieces;
    bool operator==(const VmText&) const = default;
};
struct VmArray;
using VmArrayPtr = std::shared_ptr<VmArray>;
struct VmMap;
using VmMapPtr = std::shared_ptr<VmMap>;
struct VmFact;
using VmFactPtr = std::shared_ptr<VmFact>;
using VmValue = std::variant<VmNil, bool, double, VmDegree, VmText, VmArrayPtr, VmMapPtr, VmFactPtr>;
// Public runtime spelling for canonical VM values.
using Value = VmValue;
// Typed result rendering for direct VM execution. It deliberately does not
// reconstruct AST nodes or consult Interpreter display helpers.
std::string vmValueToDisplayString(const VmValue& value);
using VmTextDecoder = std::function<std::string(std::span<const std::uint32_t>)>;
void setVmTextDecoder(VmTextDecoder decoder);
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
    IrFactRef id = 0;
    IrSymbolRef type = 0;
    enum class Origin : std::uint8_t { Asserted, Derived } origin = Origin::Asserted;
    std::uint64_t createdSequence = 0;
    std::vector<std::pair<IrSymbolRef, VmValue>> fields;
};

struct VmFactMutation { std::uint64_t sequence = 0; IrFactRef fact = 0; IrSymbolRef field = 0; };
struct VmFactProvenance { IrFactRef fact = 0; IrSymbolRef procedure = 0; bool derived = false; };
// Numeric, non-boolean evidence returned by Form's deterministic fact
// analysis.  `membership` is always in [0, 1] and is never a branch value.
struct VmGaussianProfile { double peak = 0.0; double fadesIn = 0.0; double fadesOut = 0.0; };
struct VmRankedFact { VmFactPtr fact; double effectiveAt = 0.0; double priority = 0.0; };

// Process-resident append-only fact memory. It belongs to the Form runtime,
// not to AST/parser services, and is shared by repeated VM executions in a
// daemon. Fact values remain typed and can later be indexed by type/field.
class VmFactStore {
public:
    void registerType(IrSymbolRef type, std::vector<IrSymbolRef> parents);
    IrFactRef retain(const VmFactPtr& fact);
    void mutate(const VmFactPtr& fact, IrSymbolRef field, const VmValue& value,
                IrSymbolRef procedure);
    std::vector<VmFactPtr> snapshot() const;
    std::vector<VmFactPtr> snapshot(IrSymbolRef type) const;
    std::vector<VmFactPtr> snapshotAssignableTo(IrSymbolRef type) const;
    std::vector<IrSymbolRef> hierarchyProof(IrSymbolRef child, IrSymbolRef ancestor) const;
    std::vector<IrSymbolRef> commonAncestors(IrSymbolRef left, IrSymbolRef right) const;
    std::vector<IrSymbolRef> leastCommonAncestors(IrSymbolRef left, IrSymbolRef right) const;
    std::vector<IrSymbolRef> mostGeneralCommonAncestors(IrSymbolRef left, IrSymbolRef right) const;
    // Sorts facts by descending effective time, then descending priority,
    // then stable fact identity. Missing/non-numeric fields are rejected.
    std::vector<VmRankedFact> rankByTimeAndPriority(IrSymbolRef effectiveAtField,
                                                    IrSymbolRef priorityField) const;
    std::vector<VmFactPtr> snapshotByField(IrSymbolRef field) const;
    std::vector<VmFactMutation> mutations() const;
    std::vector<VmFactProvenance> provenance() const;
    std::size_t size() const;

    // A Gaussian tail reaches 1% at each fade boundary.  Degenerate edge
    // profiles (peak equal to one boundary) reuse the non-degenerate side so
    // ratings at 0 and 100 remain continuous on the closed input domain.
    static double gaussianMembership(double value, const VmGaussianProfile& profile);

private:
    mutable std::mutex mutex_;
    IrFactRef nextId_ = 1;
    std::uint64_t nextSequence_ = 1;
    std::vector<VmFactPtr> facts_;
    std::unordered_map<IrSymbolRef, std::vector<VmFactPtr>> byType_;
    std::unordered_map<IrSymbolRef, std::vector<VmFactPtr>> byField_;
    std::unordered_map<IrSymbolRef, std::vector<IrSymbolRef>> parents_;
    std::vector<VmFactMutation> mutations_;
    std::vector<VmFactProvenance> provenance_;
    mutable std::unordered_map<IrSymbolRef, std::pair<std::uint64_t, std::vector<VmFactPtr>>> assignableCache_;
    std::uint64_t revision_ = 0;
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

// A compact, integer-only record of behavior observed while verified IR runs.
// It is deliberately not a copy of the .fir byte stream: model builders use
// these records with the fact/hierarchy state and the verified result.
enum class VmTraceKind : std::uint8_t { ExecutionBegin, ModuleInstalled, ProcedureCall, FactRetained, SsmProposal, ExecutionResult };
struct VmExecutionTrace {
    std::uint64_t sequence = 0;
    VmTraceKind kind = VmTraceKind::ExecutionBegin;
    IrSymbolRef symbol = 0;
    IrFactRef fact = 0;
    std::size_t callDepth = 0;
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
    virtual void retainFact(const VmFactPtr& fact);
    virtual void mutateFact(const VmFactPtr& fact, IrSymbolRef field, const VmValue& value);
    virtual void registerFactType(IrSymbolRef type, std::vector<IrSymbolRef> parents);
    virtual std::vector<VmFactPtr> snapshotFacts(IrSymbolRef type);
    // Modules are independently verified before this hook. A long-lived
    // runtime may retain their procedure metadata for later calls; the base
    // runtime remains intentionally stateless.
    virtual void installModule(const IrModule& module);
    virtual void recordTrace(VmTraceKind kind, IrSymbolRef symbol = 0, IrFactRef fact = 0);
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
class FelidaeKnowledgeRuntime final : public VmRuntime {
public:
    explicit FelidaeKnowledgeRuntime(std::unordered_map<IrSymbolRef, IrProcedure> procedures = {},
                             RuntimeStateModel* semanticModel = nullptr,
                             std::size_t maximumSemanticSteps = 1024,
                             std::size_t maximumCallDepth = 256,
                             std::shared_ptr<VmFactStore> factStore = {});
    VmValue loadSymbol(IrSymbolRef symbol) override;
    void storeSymbol(IrSymbolRef symbol, const VmValue& value) override;
    VmValue callSymbol(IrSymbolRef symbol, std::span<const VmValue> arguments) override;
    VmValue callSymbolNamed(IrSymbolRef symbol, std::span<const VmCallArgument> arguments) override;
    void retainFact(const VmFactPtr& fact) override;
    void mutateFact(const VmFactPtr& fact, IrSymbolRef field, const VmValue& value) override;
    void registerFactType(IrSymbolRef type, std::vector<IrSymbolRef> parents) override;
    std::vector<VmFactPtr> snapshotFacts(IrSymbolRef type) override;
    void installModule(const IrModule& module) override;
    void recordTrace(VmTraceKind kind, IrSymbolRef symbol = 0, IrFactRef fact = 0) override;
    RuntimeStateModel* runtimeStateModel() override;
    void beginExecution() override;
    void endExecution() noexcept override;
    RuntimeContext makeRuntimeContext(const FelidaeIr& ir,
                                      const VmValue& systemInput) const override;
    const std::shared_ptr<VmFactStore>& factStore() const noexcept { return factStore_; }
    std::vector<VmExecutionTrace> executionTraces() const;
    std::size_t installedModuleCount() const noexcept { return modules_.contains(0) ? modules_.size() - 1 : modules_.size(); }

private:
    // A frame has no AST payload and no shared registers. RegisterVm owns the
    // fresh register vector for every invocation; the runtime owns only the
    // procedure's lexical parameter/local bindings.
    struct VmCallFrame {
        IrSymbolRef procedure = 0;
        std::unordered_map<IrSymbolRef, VmValue> locals;
    };
    std::vector<VmCallFrame> callFrames_;
    struct VmModuleState {
        std::unordered_map<IrSymbolRef, VmValue> globals;
        std::unordered_map<IrSymbolRef, IrProcedure> procedures;
    };
    // Registry and traces are process-resident knowledge-runtime state.
    // Registers, call frames and recurrent state are deliberately excluded.
    std::unordered_map<std::uint64_t, VmModuleState> modules_;
    std::uint64_t activeModule_ = 0;
    std::vector<VmExecutionTrace> traces_;
    std::uint64_t nextTraceSequence_ = 1;
    std::size_t maximumTraceEntries_ = 65'536;
    std::shared_ptr<VmFactStore> factStore_;
    RuntimeStateModel* semanticModel_ = nullptr;
    std::size_t maximumSemanticSteps_ = 1024;
    std::size_t maximumCallDepth_ = 256;
    std::size_t procedureDepth_ = 0;
    std::size_t executionDepth_ = 0;
    std::shared_ptr<void> executionState_;
    std::shared_ptr<std::size_t> sharedSemanticSteps_;
};

// Transition spelling retained for source compatibility. New code should name
// the persistent role explicitly rather than calling it a "direct" runtime.
using DirectVmRuntime = FelidaeKnowledgeRuntime;

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
