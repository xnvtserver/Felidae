#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <bit>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Felidae {

struct IrModule;
class VerifiedIrModule;

// Instruction operands are verifier-bounded host indices. Symbol values are
// deliberately separate: source SymbolId values are always 64-bit and must
// never narrow through a pointer-width IR word on Win32.
using IrWord = std::uint32_t;
using IrConstant = std::uint64_t;
using RegisterId = std::size_t;
using IrConstantId = std::size_t;
using IrSymbolRef = std::uint64_t;
using IrFactRef = std::size_t;
using PieceId = std::uint32_t;
using PieceSequence = std::vector<PieceId>;
PieceSequence runtimeSymbolPieces(IrSymbolRef symbol);

enum class IrConstantKind : std::uint8_t { Number, Boolean, Nil, Text };

struct IrSourceMapEntry {
    std::size_t instructionWord = 0;
    struct Span { int startLine = 1; int startColumn = 1; int endLine = 1; int endColumn = 1; } sourceSpan;
};

class IrError : public std::runtime_error {
public:
    explicit IrError(const std::string& message) : std::runtime_error(message) {}
};

// Constant-pool numbers use a canonical bit representation shared by the
// compiler, binary IR loader, and register VM.
IrConstant encodeIrNumber(double value) noexcept;
double decodeIrNumber(IrConstant word) noexcept;

struct VmNil { bool operator==(const VmNil&) const = default; };
struct VmSymbol { IrSymbolRef value = 0; bool operator==(const VmSymbol&) const = default; };
struct VmDegree {
    double value = 0.0;
    explicit VmDegree(double value);
    bool operator==(const VmDegree&) const = default;
};
struct VmText {
    PieceSequence pieces;
    bool operator==(const VmText&) const = default;
};
struct VmArray;
using VmArrayPtr = std::shared_ptr<VmArray>;
struct VmMap;
using VmMapPtr = std::shared_ptr<VmMap>;
struct VmFact;
using VmFactPtr = std::shared_ptr<VmFact>;
using VmValue = std::variant<VmNil, double, VmDegree, VmText, VmSymbol, VmArrayPtr, VmMapPtr, VmFactPtr>;
static_assert([]<std::size_t... Index>(std::index_sequence<Index...>) {
    return (!std::is_same_v<bool, std::variant_alternative_t<Index, VmValue>> && ...);
}(std::make_index_sequence<std::variant_size_v<VmValue>>{}),
"VM truth values must be double 0.0 or 1.0, never bool");
// Public runtime spelling for canonical VM values.
using Value = VmValue;

// Stable model/dataset contract.  Do not persist VmValue::index(): changing
// the in-memory variant layout must never reinterpret an existing corpus or
// model artifact.
enum class RuntimeValueKind : std::uint8_t {
    Nil = 1,
    Number = 2,
    Degree = 3,
    Text = 4,
    Array = 5,
    Map = 6,
    Fact = 7,
    Symbol = 8,
};
RuntimeValueKind runtimeValueKind(const VmValue& value) noexcept;
using VmSymbolDecoder = std::function<std::string(IrSymbolRef)>;
using VmTextDecoder = std::function<std::string(std::span<const PieceId>)>;
// Rendering is an adapter boundary, not VM state. Form consumes only IDs and
// typed values; callers may inject SentencePiece and symbol decoders for UI.
struct VmDisplayContext {
    VmTextDecoder textDecoder;
    VmSymbolDecoder symbolDecoder;
};
std::string vmValueToDisplayString(const VmValue& value,
                                   const VmDisplayContext& context = {});
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
struct VmKnowledgeSnapshot {
    std::vector<IrSymbolRef> factTypes;
    // Sorted, bounded fact population by type. This is a compact runtime
    // observation, not a second fact store or a source-text representation.
    std::vector<std::pair<IrSymbolRef, std::uint32_t>> factTypeCounts;
    std::vector<std::pair<IrSymbolRef, IrSymbolRef>> hierarchyEdges;
};
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
    VmKnowledgeSnapshot knowledgeSnapshot() const;
    // Rebuilds `snapshot` only when fact/type state changed. Runtime SSM
    // dispatch uses this to avoid sorting the same knowledge graph on every
    // SemanticEval instruction.
    void refreshKnowledgeSnapshot(std::uint64_t& knownRevision,
                                  VmKnowledgeSnapshot& snapshot) const;
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
// A semantic operation is a permanent ISA ID plus typed VM values. It never
// uses a source symbol, text, or SentencePiece token as executable semantics.
struct RuntimeOperation {
    std::uint16_t id = 0;
};

// A compact, integer-only record of behavior observed while verified IR runs.
// It is deliberately not a copy of the .bin byte stream: model builders use
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
    // Refreshed immediately before each semantic operation. It contains only
    // integer IDs, so a runtime model observes the current fact/hierarchy
    // state without source parsing or text search.
    VmKnowledgeSnapshot knowledge;
    std::uint64_t knowledgeRevision = std::numeric_limits<std::uint64_t>::max();
};

class RuntimeStateModel {
public:
    virtual ~RuntimeStateModel() = default;
    // Called once for each top-level Form runtime execution. Backends use
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
    virtual VmValue callNativeSymbol(IrSymbolRef symbol);
    virtual void retainFact(const VmFactPtr& fact);
    virtual void mutateFact(const VmFactPtr& fact, IrSymbolRef field, const VmValue& value);
    virtual void registerFactType(IrSymbolRef type, std::vector<IrSymbolRef> parents);
    virtual std::vector<VmFactPtr> snapshotFacts(IrSymbolRef type);
    virtual std::vector<IrSymbolRef> hierarchyProof(IrSymbolRef child, IrSymbolRef ancestor);
    virtual std::vector<IrSymbolRef> commonAncestors(IrSymbolRef left, IrSymbolRef right);
    virtual std::vector<IrSymbolRef> leastCommonAncestors(IrSymbolRef left, IrSymbolRef right);
    virtual std::vector<IrSymbolRef> mostGeneralCommonAncestors(IrSymbolRef left, IrSymbolRef right);
    virtual std::vector<VmRankedFact> rankFacts(IrSymbolRef effectiveAtField,
                                                IrSymbolRef priorityField);
    // Modules are independently verified before this hook. A long-lived
    // runtime may retain their procedure metadata for later calls; the base
    // runtime remains intentionally stateless.
    virtual void installIrModule(const IrModule& module);
    virtual IrSymbolRef resolveSymbol(IrSymbolRef moduleSymbol) const;
    virtual void enterProcedure(IrSymbolRef procedure,
                                std::span<const IrSymbolRef> parameters,
                                std::span<const VmValue> arguments);
    virtual void leaveProcedure() noexcept;
    virtual void recordTrace(VmTraceKind kind, IrSymbolRef symbol = 0, IrFactRef fact = 0);
    // Null means this execution has no learned semantic backend. SemanticEval
    // then fails in a controlled manner; exact opcodes never use this hook.
    virtual RuntimeStateModel* runtimeStateModel();
    virtual void beginExecution();
    virtual void endExecution() noexcept;
    virtual RuntimeContext makeRuntimeContext(const VmValue& systemInput) const;
    // A long-lived runtime updates operation context at the semantic boundary;
    // the base runtime has no knowledge state to add.
    virtual void refreshRuntimeContext(RuntimeContext& context) const;
    // Runtime-owned handles (facts, native objects, hierarchy cursors) can be
    // checked here before a learned result enters a VM register. The default
    // admits only structurally valid VM values.
    virtual bool validateSemanticResult(const VmValue& value,
                                        const RuntimeContext& context) const;
    // Branch protocol, not generic truthiness. The only truth values are the
    // doubles 0.0 and 1.0; facts, text, containers, and other numbers are not
    // coerced. Nil and 0.0 take the false branch, while 1.0 continues.
    virtual bool shouldBranchFalse(const VmValue& value) const;
};

// Knowledge services used by verified ISA programs. The VM boundary stays
// explicit: ISA operations reach only this typed runtime contract and never
// implicitly enter the parser or Interpreter.
class FelidaeKnowledgeRuntime final : public VmRuntime {
public:
    explicit FelidaeKnowledgeRuntime(RuntimeStateModel* semanticModel = nullptr,
                             std::size_t maximumSemanticSteps = 1024,
                             std::size_t maximumCallDepth = 256,
                             std::shared_ptr<VmFactStore> factStore = {});
    VmValue loadSymbol(IrSymbolRef symbol) override;
    void storeSymbol(IrSymbolRef symbol, const VmValue& value) override;
    void retainFact(const VmFactPtr& fact) override;
    void mutateFact(const VmFactPtr& fact, IrSymbolRef field, const VmValue& value) override;
    void registerFactType(IrSymbolRef type, std::vector<IrSymbolRef> parents) override;
    std::vector<VmFactPtr> snapshotFacts(IrSymbolRef type) override;
    std::vector<IrSymbolRef> hierarchyProof(IrSymbolRef child, IrSymbolRef ancestor) override;
    std::vector<IrSymbolRef> commonAncestors(IrSymbolRef left, IrSymbolRef right) override;
    std::vector<IrSymbolRef> leastCommonAncestors(IrSymbolRef left, IrSymbolRef right) override;
    std::vector<IrSymbolRef> mostGeneralCommonAncestors(IrSymbolRef left, IrSymbolRef right) override;
    std::vector<VmRankedFact> rankFacts(IrSymbolRef effectiveAtField,
                                        IrSymbolRef priorityField) override;
    void installIrModule(const IrModule& module) override;
    IrSymbolRef resolveSymbol(IrSymbolRef moduleSymbol) const override;
    void enterProcedure(IrSymbolRef procedure,
                        std::span<const IrSymbolRef> parameters,
                        std::span<const VmValue> arguments) override;
    void leaveProcedure() noexcept override;
    void recordTrace(VmTraceKind kind, IrSymbolRef symbol = 0, IrFactRef fact = 0) override;
    RuntimeStateModel* runtimeStateModel() override;
    void beginExecution() override;
    void endExecution() noexcept override;
    RuntimeContext makeRuntimeContext(const VmValue& systemInput) const override;
    void refreshRuntimeContext(RuntimeContext& context) const override;
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
        std::vector<IrSymbolRef> symbols;
    };
    // Registry and traces are process-resident knowledge-runtime state.
    // Registers, call frames and recurrent state are deliberately excluded.
    std::unordered_map<std::uint64_t, VmModuleState> modules_;
    std::uint64_t activeModule_ = 0;
    std::deque<VmExecutionTrace> traces_;
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

class RegisterVm {
public:
    explicit RegisterVm(std::size_t maximumInstructionSteps = 10'000'000);
    VmValue executeMain(const VerifiedIrModule& module, VmRuntime& runtime,
                        VmValue systemInput = VmNil{});

private:
    VmValue executeIrProgram(const IrModule& module, const struct FelidaeIr& program,
                             VmRuntime& runtime, VmValue systemInput,
                             std::size_t callDepth, std::size_t& instructionSteps);
    std::size_t maximumInstructionSteps_;
};

} // namespace Felidae
