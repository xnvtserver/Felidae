#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace at {
class Tensor;
}

namespace Felidae {

enum class TensorOperation : std::uint8_t;

struct FelidaeIr;
struct IrModule;
class VerifiedIrModule;

// Executable operands and module-local references use the same fixed-width
// word type stored by FELBIR. A symbol reference is a one-based index into the
// module's full SentencePiece-sequence table, never a source hash.
using IrWord = std::uint32_t;
using IrConstant = std::uint64_t;
using RegisterId = IrWord;
using IrConstantId = IrWord;
using IrSymbolRef = IrWord;
using IrFactRef = std::size_t;
using PieceId = std::uint32_t;
using PieceSequence = std::vector<PieceId>;

enum class IrConstantKind : std::uint8_t { Number, Boolean, Nil, Text };

struct IrSourceMapEntry {
  std::size_t instructionWord = 0;
  struct Span {
    int startLine = 1;
    int startColumn = 1;
    int endLine = 1;
    int endColumn = 1;
  } sourceSpan;
};

class IrError : public std::runtime_error {
public:
  explicit IrError(const std::string &message) : std::runtime_error(message) {}
};

// Constant-pool numbers use a canonical bit representation shared by the
// compiler, binary IR loader, and register VM.
IrConstant encodeIrNumber(double value) noexcept;
double decodeIrNumber(IrConstant word) noexcept;
// Canonical instruction-width decoder shared by verification, execution, and
// compiler-side inspection of generated IR. It also rejects invalid or
// truncated instructions, so callers must not maintain parallel width tables.
std::size_t irInstructionWidth(const FelidaeIr &ir, std::size_t pc);

struct VmNil {
  bool operator==(const VmNil &) const = default;
};
struct VmSymbol {
  IrSymbolRef value = 0;
  bool operator==(const VmSymbol &) const = default;
};
struct VmDegree {
  double value = 0.0;
  explicit VmDegree(double value);
  bool operator==(const VmDegree &) const = default;
};
struct VmText {
  PieceSequence pieces;
  bool operator==(const VmText &) const = default;
};
struct VmArray;
using VmArrayPtr = std::shared_ptr<VmArray>;
struct VmMap;
using VmMapPtr = std::shared_ptr<VmMap>;
struct VmTextMap;
using VmTextMapPtr = std::shared_ptr<VmTextMap>;
struct VmFact;
// Published facts are immutable snapshots. Mutable `VmFact` builders may be
// created locally, but conversion to VmFactPtr removes write access before a
// value enters VmValue or VmFactStore.
using VmFactPtr = std::shared_ptr<const VmFact>;
// The portable VM owns a real LibTorch tensor without including Torch headers
// in Form. Only TensorRuntime creates or dereferences `storage`; shape is kept
// here so validation and fallback display remain backend-independent.
struct VmTensor {
  std::shared_ptr<at::Tensor> storage;
  std::vector<std::int64_t> shape;
};
using VmTensorPtr = std::shared_ptr<VmTensor>;
using VmValue =
    std::variant<VmNil, double, VmDegree, VmText, VmSymbol, VmArrayPtr,
                 VmMapPtr, VmFactPtr, VmTensorPtr, VmTextMapPtr>;
static_assert(
    []<std::size_t... Index>(std::index_sequence<Index...>) {
      return (
          !std::is_same_v<bool, std::variant_alternative_t<Index, VmValue>> &&
          ...);
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
  Tensor = 9,
  TextMap = 10,
};
RuntimeValueKind runtimeValueKind(const VmValue &value) noexcept;
using VmSymbolDecoder = std::function<std::string(IrSymbolRef)>;
using VmTextDecoder = std::function<std::string(std::span<const PieceId>)>;
using VmTextEncoder = std::function<PieceSequence(std::string_view)>;
using VmTensorDecoder = std::function<std::string(const VmTensor &)>;
// Rendering is an adapter boundary, not VM state. Form consumes only IDs and
// typed values; callers may inject SentencePiece and symbol decoders for UI.
struct VmDisplayContext {
  VmTextDecoder textDecoder;
  VmSymbolDecoder symbolDecoder;
  VmTensorDecoder tensorDecoder;
};
std::string vmValueToDisplayString(const VmValue &value,
                                   const VmDisplayContext &context = {});
struct VmArray {
  std::vector<VmValue> values;
};
struct VmMap {
  std::vector<std::pair<IrSymbolRef, VmValue>> entries;
};
// Dynamic object keys (for JSON and CSV) are tokenized text, never raw UTF-8
// or module-local symbol references. Static source maps continue to use
// VmMap; this separate type keeps both identity contracts unambiguous.
struct VmTextMap {
  std::vector<std::pair<PieceSequence, VmValue>> entries;
};
// Facts retain a separate type tag.  They are deliberately not aliases for
// maps: map-shaped values must not acquire fact semantics merely by carrying
// a similarly named field.
struct VmFact {
  // Identity metadata becomes immutable once VmFactStore::retain succeeds.
  // Change retained fields only through VmFactStore::mutate so indexes and
  // provenance remain synchronized.
  IrFactRef id = 0;
  IrSymbolRef type = 0;
  enum class Origin : std::uint8_t {
    Asserted,
    Derived
  } origin = Origin::Asserted;
  std::uint64_t createdSequence = 0;
  std::vector<std::pair<IrSymbolRef, VmValue>> fields;
};

struct VmFactMutation {
  std::uint64_t sequence = 0;
  IrFactRef fact = 0;
  IrSymbolRef field = 0;
};
struct VmFactProvenance {
  IrFactRef fact = 0;
  IrSymbolRef procedure = 0;
  bool derived = false;
};
struct VmKnowledgeSnapshot {
  std::vector<IrSymbolRef> factTypes;
  // Sorted, bounded fact population by type. This is a compact runtime
  // observation, not a second fact store or a source-text representation.
  std::vector<std::pair<IrSymbolRef, std::uint32_t>> factTypeCounts;
  std::vector<std::pair<IrSymbolRef, IrSymbolRef>> hierarchyEdges;
};
// Numeric, non-boolean evidence returned by Form's deterministic fact
// analysis.  `membership` is always in [0, 1] and is never a branch value.
struct VmGaussianProfile {
  double peak = 0.0;
  double fadesIn = 0.0;
  double fadesOut = 0.0;
};
struct VmRankedFact {
  VmFactPtr fact;
  double effectiveAt = 0.0;
  double priority = 0.0;
};
struct VmFactStoreRevisions {
  std::uint64_t hierarchy = 0;
  std::uint64_t membership = 0;
  std::uint64_t content = 0;

  bool operator==(const VmFactStoreRevisions &) const = default;
};

// Process-resident append-only fact memory. It belongs to the Form runtime,
// not to AST/parser services, and is shared by repeated VM executions in a
// daemon. Type and field indexes are authoritative; callers must not mutate a
// retained fact directly.
class VmFactStore {
public:
  void registerType(IrSymbolRef type, std::vector<IrSymbolRef> parents);
  VmFactPtr retain(const VmFactPtr &fact);
  VmFactPtr mutate(const VmFactPtr &fact, IrSymbolRef field,
                   const VmValue &value, IrSymbolRef procedure);
  std::vector<VmFactPtr> snapshot() const;
  std::vector<VmFactPtr> snapshot(IrSymbolRef type) const;
  std::vector<VmFactPtr> snapshotAssignableTo(IrSymbolRef type) const;
  std::vector<IrSymbolRef> hierarchyProof(IrSymbolRef child,
                                          IrSymbolRef ancestor) const;
  std::vector<IrSymbolRef> commonAncestors(IrSymbolRef left,
                                           IrSymbolRef right) const;
  std::vector<IrSymbolRef> leastCommonAncestors(IrSymbolRef left,
                                                IrSymbolRef right) const;
  std::vector<IrSymbolRef> mostGeneralCommonAncestors(IrSymbolRef left,
                                                      IrSymbolRef right) const;
  // Sorts facts by descending effective time, then descending priority,
  // then stable fact identity. Missing/non-numeric fields are rejected.
  std::vector<VmRankedFact>
  rankByTimeAndPriority(IrSymbolRef effectiveAtField,
                        IrSymbolRef priorityField) const;
  std::vector<VmFactPtr> snapshotByField(IrSymbolRef field) const;
  std::vector<VmFactMutation> mutations() const;
  std::vector<VmFactProvenance> provenance() const;
  VmKnowledgeSnapshot knowledgeSnapshot() const;
  // Rebuilds `snapshot` only when fact/type state changed. Runtime SSM
  // dispatch uses this to avoid sorting the same knowledge graph on every
  // SemanticEval instruction.
  void refreshKnowledgeSnapshot(std::uint64_t &knownRevision,
                                VmKnowledgeSnapshot &snapshot) const;
  VmFactStoreRevisions revisions() const;
  std::size_t size() const;

  // Persistence ownership: which source (e.g. a CSV file path) a fact was
  // imported from, if any. db.sync consults this to serialize only the
  // facts that actually belong to the target file, instead of the whole
  // store -- it must never infer ownership from fact type alone, since two
  // different files can share one fact type. A fact with no recorded
  // source is simply excluded from any snapshotBySource() result; only
  // recordSource() call sites (currently just CSV import) decide what
  // counts as "belongs to this file".
  void recordSource(IrFactRef fact, std::string source);
  std::vector<VmFactPtr> snapshotBySource(std::string_view source) const;

  // A Gaussian tail reaches 1% at each fade boundary.  Degenerate edge
  // profiles (peak equal to one boundary) reuse the non-degenerate side so
  // ratings at 0 and 100 remain continuous on the closed input domain.
  static double gaussianMembership(double value,
                                   const VmGaussianProfile &profile);

private:
  const std::unordered_set<IrSymbolRef> &
  ancestorClosureLocked(IrSymbolRef type) const;
  bool isAssignableToLocked(IrSymbolRef candidate, IrSymbolRef expected) const;

  mutable std::mutex mutex_;
  IrFactRef nextId_ = 1;
  std::uint64_t nextSequence_ = 1;
  std::vector<VmFactPtr> facts_;
  std::unordered_map<IrSymbolRef, std::vector<VmFactPtr>> byType_;
  std::unordered_map<IrSymbolRef, std::vector<VmFactPtr>> byField_;
  std::unordered_map<IrSymbolRef, std::vector<IrSymbolRef>> parents_;
  std::unordered_map<IrFactRef, std::string> factSource_;
  std::vector<VmFactMutation> mutations_;
  std::vector<VmFactProvenance> provenance_;
  mutable std::unordered_map<
      IrSymbolRef,
      std::pair<std::uint64_t, std::unordered_set<IrSymbolRef>>>
      ancestorClosureCache_;
  std::uint64_t hierarchyRevision_ = 0;
  std::uint64_t membershipRevision_ = 0;
  std::uint64_t contentRevision_ = 0;
  // The runtime SSM snapshot contains only hierarchy edges and per-type fact
  // counts. Its revision therefore excludes ordinary field-value mutation.
  std::uint64_t knowledgeRevision_ = 0;
};
// A semantic operation is a permanent IR ID plus typed VM values. It never
// uses a source symbol, text, or SentencePiece token as executable semantics.
struct RuntimeOperation {
  std::uint16_t id = 0;
};

// This object is created for a top-level VM execution. Nested procedure VMs
// receive the same execution state, while separate calls to RegisterVm never
// share it. A model state is opaque to the VM but lifetime-bound to this scope.
struct RuntimeContext {
  std::size_t maximumSemanticSteps = 1024;
  std::size_t semanticSteps = 0;
  std::shared_ptr<void> executionState;
  std::shared_ptr<std::size_t> sharedSemanticSteps;
  // Refreshed immediately before each semantic operation. Module-local
  // references are resolved through symbolTable to their complete
  // SentencePiece sequences at the model boundary.
  VmKnowledgeSnapshot knowledge;
  const std::vector<PieceSequence> *symbolTable = nullptr;
  std::uint64_t knowledgeRevision = std::numeric_limits<std::uint64_t>::max();
};

class RuntimeStateModel {
public:
  virtual ~RuntimeStateModel() = default;
  // Called once for each top-level Form runtime execution. Backends use
  // this to allocate their recurrent/SSM state without retaining it across
  // requests. The default model is stateless.
  virtual std::shared_ptr<void> createExecutionState() { return {}; }
  virtual Value evaluate(const RuntimeOperation &operation,
                         std::span<const Value> inputs,
                         RuntimeContext &context) = 0;
};

class TensorRuntime {
public:
  virtual ~TensorRuntime() = default;
  // Returned tensors must own finite backend storage. RegisterVm validates the
  // portable wrapper; the backend owns element-level validation because Form
  // intentionally cannot dereference at::Tensor on unsupported platforms.
  virtual Value evaluateTensor(TensorOperation operation,
                               std::span<const Value> inputs,
                               std::span<const PieceSequence> symbolTable) = 0;
};

class VmRuntime {
public:
  virtual ~VmRuntime() = default;
  virtual VmValue loadSymbol(IrSymbolRef symbol);
  virtual void storeSymbol(IrSymbolRef symbol, const VmValue &value);
  virtual VmFactPtr retainFact(const VmFactPtr &fact);
  virtual VmFactPtr mutateFact(const VmFactPtr &fact, IrSymbolRef field,
                               const VmValue &value);
  virtual void registerFactType(IrSymbolRef type,
                                std::vector<IrSymbolRef> parents);
  virtual std::vector<VmFactPtr> snapshotFacts(IrSymbolRef type);
  virtual std::vector<IrSymbolRef> hierarchyProof(IrSymbolRef child,
                                                  IrSymbolRef ancestor);
  virtual std::vector<IrSymbolRef> commonAncestors(IrSymbolRef left,
                                                   IrSymbolRef right);
  virtual std::vector<IrSymbolRef> leastCommonAncestors(IrSymbolRef left,
                                                        IrSymbolRef right);
  virtual std::vector<IrSymbolRef>
  mostGeneralCommonAncestors(IrSymbolRef left, IrSymbolRef right);
  virtual std::vector<VmRankedFact> rankFacts(IrSymbolRef effectiveAtField,
                                              IrSymbolRef priorityField);
  // Modules are independently verified before this hook. A long-lived
  // runtime may retain their procedure metadata for later calls; the base
  // runtime remains intentionally stateless.
  virtual void installIrModule(const IrModule &module);
  virtual IrSymbolRef resolveSymbol(IrSymbolRef moduleSymbol) const;
  virtual std::span<const PieceSequence> runtimeSymbolTable() const;
  virtual void enterProcedure(IrSymbolRef procedure,
                              std::span<const IrSymbolRef> parameters,
                              std::span<const VmValue> arguments);
  virtual void leaveProcedure() noexcept;
  // Null means this execution has no learned semantic backend. SemanticEval
  // then fails in a controlled manner; exact opcodes never use this hook.
  virtual RuntimeStateModel *runtimeStateModel();
  virtual TensorRuntime *tensorRuntime();
  virtual std::string decodeText(std::span<const PieceId> pieces) const;
  virtual PieceSequence encodeText(std::string_view text) const;
  virtual VmText readFile(std::span<const PieceId> path) const;
  virtual VmValue importCsvFacts(std::span<const PieceId> data,
                                 std::span<const PieceId> type,
                                 std::span<const PieceId> source);
  virtual double syncDatabase(std::span<const PieceId> path);
  virtual void beginExecution();
  virtual void endExecution() noexcept;
  virtual RuntimeContext makeRuntimeContext(const VmValue &systemInput) const;
  // A long-lived runtime updates operation context at the semantic boundary;
  // the base runtime has no knowledge state to add.
  virtual void refreshRuntimeContext(RuntimeContext &context) const;
  // Branch protocol, not generic truthiness. The only truth values are the
  // doubles 0.0 and 1.0; facts, text, containers, and other numbers are not
  // coerced. Nil and 0.0 take the false branch, while 1.0 continues.
  virtual bool shouldBranchFalse(const VmValue &value) const;
};

// Knowledge services used by verified IR programs. The VM boundary stays
// explicit: IR operations reach only this typed runtime contract and never
// implicitly enter the parser or Interpreter.
class FelidaeKnowledgeRuntime final : public VmRuntime {
public:
  explicit FelidaeKnowledgeRuntime(RuntimeStateModel *semanticModel = nullptr,
                                   std::size_t maximumSemanticSteps = 1024,
                                   std::size_t maximumCallDepth = 256,
                                   std::shared_ptr<VmFactStore> factStore = {},
                                   TensorRuntime *tensorRuntime = nullptr,
                                   VmTextDecoder textDecoder = {},
                                   VmTextEncoder textEncoder = {});
  VmValue loadSymbol(IrSymbolRef symbol) override;
  void storeSymbol(IrSymbolRef symbol, const VmValue &value) override;
  VmFactPtr retainFact(const VmFactPtr &fact) override;
  VmFactPtr mutateFact(const VmFactPtr &fact, IrSymbolRef field,
                       const VmValue &value) override;
  void registerFactType(IrSymbolRef type,
                        std::vector<IrSymbolRef> parents) override;
  std::vector<VmFactPtr> snapshotFacts(IrSymbolRef type) override;
  std::vector<IrSymbolRef> hierarchyProof(IrSymbolRef child,
                                          IrSymbolRef ancestor) override;
  std::vector<IrSymbolRef> commonAncestors(IrSymbolRef left,
                                           IrSymbolRef right) override;
  std::vector<IrSymbolRef> leastCommonAncestors(IrSymbolRef left,
                                                IrSymbolRef right) override;
  std::vector<IrSymbolRef>
  mostGeneralCommonAncestors(IrSymbolRef left, IrSymbolRef right) override;
  std::vector<VmRankedFact> rankFacts(IrSymbolRef effectiveAtField,
                                      IrSymbolRef priorityField) override;
  void installIrModule(const IrModule &module) override;
  IrSymbolRef resolveSymbol(IrSymbolRef moduleSymbol) const override;
  std::span<const PieceSequence> runtimeSymbolTable() const override;
  void enterProcedure(IrSymbolRef procedure,
                      std::span<const IrSymbolRef> parameters,
                      std::span<const VmValue> arguments) override;
  void leaveProcedure() noexcept override;
  RuntimeStateModel *runtimeStateModel() override;
  TensorRuntime *tensorRuntime() override;
  std::string decodeText(std::span<const PieceId> pieces) const override;
  PieceSequence encodeText(std::string_view text) const override;
  VmText readFile(std::span<const PieceId> path) const override;
  VmValue importCsvFacts(std::span<const PieceId> data,
                         std::span<const PieceId> type,
                         std::span<const PieceId> source) override;
  double syncDatabase(std::span<const PieceId> path) override;
  void beginExecution() override;
  void endExecution() noexcept override;
  RuntimeContext makeRuntimeContext(const VmValue &systemInput) const override;
  void refreshRuntimeContext(RuntimeContext &context) const override;
  const std::shared_ptr<VmFactStore> &factStore() const noexcept {
    return factStore_;
  }
  std::size_t installedModuleCount() const noexcept {
    return module_.symbolTable.empty() ? 0 : 1;
  }

private:
  IrSymbolRef internRuntimeSymbol(PieceSequence pieces);
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
    std::vector<PieceSequence> symbolTable;
    // One entry per module-local symbol. Values are one-based indexes into
    // runtimeSymbolTable_, which remains stable across installed modules.
    std::vector<IrSymbolRef> runtimeSymbols;
  };
  // Module globals are process-resident knowledge-runtime state. Registers,
  // call frames and recurrent state are deliberately excluded.
  VmModuleState module_;
  std::map<PieceSequence, IrSymbolRef> runtimeSymbolIds_;
  std::vector<PieceSequence> runtimeSymbolTable_;
  std::unordered_set<IrSymbolRef> registeredFactTypes_;
  std::shared_ptr<VmFactStore> factStore_;
  RuntimeStateModel *semanticModel_ = nullptr;
  TensorRuntime *tensorRuntime_ = nullptr;
  VmTextDecoder textDecoder_;
  VmTextEncoder textEncoder_;
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
  VmValue executeMain(const VerifiedIrModule &module, VmRuntime &runtime,
                      VmValue systemInput = VmNil{});

private:
  VmValue executeIrProgram(const IrModule &module,
                           const struct FelidaeIr &program, VmRuntime &runtime,
                           VmValue systemInput, std::size_t callDepth,
                           std::size_t &instructionSteps);
  std::size_t maximumInstructionSteps_;
};

} // namespace Felidae
