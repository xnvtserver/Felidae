#pragma once

#include "AST.h"
#include "Env.h"
#include "NativeRuntime.h"
#include "ParserMetrics.h"
#include "TypeHierarchy.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Felidae {

class RocksFactStore;
class WordVocabulary;
struct StoredFactIndex;
struct StoredLink;
struct StoredClassEdge;

class InterpreterError : public std::runtime_error {
public:
    explicit InterpreterError(const std::string& msg) : std::runtime_error(msg) {}
};

// An exception object raised by throw(exception: {kind: "...", message: "..."}).
// It is an InterpreterError, so an uncaught one reports its message; try/catch
// binds {kind, message} to the catch variable.
class FelidaeException : public InterpreterError {
public:
    FelidaeException(std::string kind, const std::string& message)
        : InterpreterError(message), kind(std::move(kind)) {}
    std::string kind;
};

class Interpreter {
public:
    using MetricValues = std::map<std::string, std::uint64_t>;
    using ClauseList = std::vector<std::shared_ptr<ClauseStmt>>;

    Interpreter();
    ~Interpreter();
    void openDatabase(const std::filesystem::path& directory);
    void configureDatabase(
        const std::map<std::string, std::uint64_t>& options);
    // Console builtins use streams owned by the caller; the default terminal
    // streams remain the fallback for embedded callers that do not configure them.
    void setIoStreams(std::istream& input, std::ostream& output) noexcept {
        inputStream_ = &input;
        outputStream_ = &output;
    }

    void addProgram(const Program& program);
    // Fast registration boundary for a parser that emits one statement at a
    // time. Facts do not need a transient Program/AST container; rules and
    // globals still use addProgram so complete validation remains identical.
    void addStreamedStatement(std::shared_ptr<Statement> statement);
    void addClause(std::shared_ptr<ClauseStmt> clause);
    void addImport(const std::filesystem::path& baseDir, const std::string& pattern);
    // Streaming module publication is transactional: callers may register
    // statements one at a time and either publish the complete module or
    // restore the previous immutable roots without retaining a module AST.
    void beginModuleTransaction();
    void commitModuleTransaction();
    void rollbackModuleTransaction();

    std::vector<Solution> solve(const std::vector<std::shared_ptr<Goal>>& queryGoals,
                                size_t maxSolutions = 1000);

    std::shared_ptr<Expr> resolveExpr(const std::shared_ptr<Expr>& expr, const Env& env) const;
    std::string exprToString(const std::shared_ptr<Expr>& expr, const Env& env) const;
    bool hasMethod(const std::string& name);
    bool hasAutoEntryCall() const;
    bool hasGlobal(const std::string& name) const;
    std::shared_ptr<Expr> evaluateGlobal(const std::string& name) const;
    std::shared_ptr<Expr> evaluateExpressionText(const std::string& text);
    std::shared_ptr<Expr> callMain(const std::shared_ptr<Expr>& systemInput);
    std::shared_ptr<Expr> callAutoEntry();
    std::string valueToString(const std::shared_ptr<Expr>& value) const;
    std::string valueToDisplayString(const std::shared_ptr<Expr>& value) const;
    std::string valueToDebugString(const std::shared_ptr<Expr>& value) const;
    std::string runtimeMetricsJson() const;
    // Read-only snapshots used by interactive diagnostics. They are collected
    // only on request and add no work to normal file execution.
    MetricValues runtimeCounters() const;
    MetricValues databaseStatistics() const;
    void recordStreamedModuleMicros(std::size_t micros);
    void recordParserMetrics(const ParserMetrics& metrics);
    // Loads one source file in a module transaction (the caller's, when one is
    // open) with currentLoadingFile_ set to it. Class source locators, fact
    // origins and relative imports all depend on that, so the entry file must
    // be loaded through here as well as imports.
    void loadProgramFile(const std::filesystem::path& file,
                         ParserMetrics* metrics = nullptr);
    std::size_t syncFactSource(const std::filesystem::path& file);
    std::shared_ptr<OperatorRegistry> operatorRegistry() const { return operators_; }
    std::shared_ptr<WordVocabulary> tokenizer() const { return tokenizer_; }

    // Real (not simulated) execution control for a driving debugger: called
    // once per goal, immediately before it runs, from solveIterative's
    // dispatch loop - the same point every top-level goal and every method
    // body's goals pass through (method calls recurse back into that same
    // loop via solveRecursive), so one hook covers both without threading a
    // callback through every call site. The hook receives the live Env for
    // that goal, so `print`/`locals` during a pause see real bound values,
    // not placeholders, and the real method call-stack depth (methodCallDepth_,
    // shared with the recursion-limit check in solveMethodCall) rather than
    // solveIterative's own frame nesting depth, which also counts group/if/or
    // bodies and - for a method called from expression position, e.g. `b :=
    // helper(x: a)` - does not increase at all. Costs one null std::function
    // check per goal when unset (the default), so running without a debugger
    // attached pays nothing beyond that. Only main.cpp's CLI debug driver
    // installs one; the interpreter itself has no notion of breakpoints or
    // step modes.
    using GoalHook = std::function<void(const Goal& goal, const Env& env, std::size_t callDepth)>;
    void setGoalHook(GoalHook hook) { goalHook_ = std::move(hook); }

private:
    struct ThreadTask {
        explicit ThreadTask(std::string functionName) : functionName(std::move(functionName)) {}
        std::string functionName;
        std::thread worker;
        std::string status = "created";
        std::string result;
        std::string error;
        bool started = false;
    };
    struct MethodParamPlan {
        std::string localName;
        std::string typeName;
        LanguageTypeId typeId = LanguageTypeId::Unknown;
        bool typedParam = false;
        bool builtinType = false;
    };
    struct MethodRuntimeInfo {
        size_t callCount = 0;
        bool paramsPrepared = false;
        bool cacheEligible = false;
        std::vector<MethodParamPlan> params;
    };
    struct SolveCacheEntry {
        std::vector<Solution> solutions;
        std::list<std::string>::iterator recency;
        std::size_t estimatedBytes = 0;
    };
    struct ClauseBucket {
        std::string name;
        ClauseList clauses;
    };
    using ClauseTable = std::unordered_map<SymbolId, std::vector<ClauseBucket>>;

    struct ProvenanceNode {
        // Reuses ClauseKind (AST.h) rather than a private Fact/Rule enum:
        // every provenance node originates from a ClauseStmt that already
        // carries this same classification, so a second copy would only be
        // able to drift from it.
        ClauseKind kind = ClauseKind::Fact;
        std::uint64_t factId = 0;
        std::string rule;
        SourceSpan span;
        std::vector<std::size_t> parents;
    };
    struct TableAnswer {
        Call call;
        std::vector<std::size_t> provenance;
    };
    struct PredicateTable {
        std::string name;
        SymbolId nameId = 0;
        std::vector<TableAnswer> answers;
        std::vector<std::size_t> delta;
        std::unordered_map<std::string, std::size_t> answerByKey;
    };
    struct TableEvaluation {
        SymbolId rootId = 0;
        std::string rootName;
        std::uint64_t hierarchyGeneration = 0;
        std::unordered_map<SymbolId, std::uint64_t> callableGenerations;
        std::unordered_map<SymbolId, PredicateTable> predicates;
        std::vector<ProvenanceNode> provenance;
        std::size_t rounds = 0;
        std::size_t deltaAnswers = 0;
    };
    struct TableBinding {
        Env env;
        std::vector<std::size_t> provenance;
    };

    struct ModuleTransactionState {
        std::shared_ptr<ClauseTable> clauses;
        std::shared_ptr<OperatorRegistry> operators;
        std::unordered_map<PatternId, std::vector<std::shared_ptr<ClauseStmt>>> operatorClauses;
        std::vector<Call> autoEntryCalls;
        std::vector<std::shared_ptr<Expr>> autoEntryResults;
        TypeHierarchy hierarchy;
        GlobalEnv globals;
        std::unordered_map<SymbolId, std::shared_ptr<ClassStmt>> classDefinitions;
        std::unordered_set<std::string> persistedClassSchemas;
        struct FactTypeContractSnapshot {
            std::vector<std::string> fields;
            std::vector<std::string> keyFields;
            std::vector<std::vector<std::string>> indexes;
            bool declaredClass = false;
            bool openShape = false;
        };
        std::unordered_map<std::string, FactTypeContractSnapshot> factTypeContracts;
        std::set<std::filesystem::path> loadedFiles;
        std::unordered_set<std::string> packageDiscoveryAttempts;
        std::unordered_map<const ClauseStmt*, std::filesystem::path> clauseOrigins;
        std::uint64_t programGeneration = 1;
        std::unordered_map<SymbolId, std::uint64_t> symbolGenerations;
        std::size_t moduleLoads = 0;
        std::size_t cacheInvalidationDepth = 0;
        bool pendingCacheInvalidation = false;
        std::unordered_map<SymbolId, std::string> contraries;
    };

    std::shared_ptr<ClauseTable> clauses_ = std::make_shared<ClauseTable>();
    std::shared_ptr<OperatorRegistry> operators_ = std::make_shared<OperatorRegistry>();
    std::shared_ptr<WordVocabulary> tokenizer_;
    std::unordered_map<PatternId, std::vector<std::shared_ptr<ClauseStmt>>> operatorClauses_;
    std::unique_ptr<ModuleTransactionState> moduleTransaction_;
    std::vector<Call> autoEntryCalls_;
    std::vector<std::shared_ptr<Expr>> autoEntryResults_;
    TypeHierarchy hierarchy_;
    GlobalEnv globals_;
    // Class declarations are retained as AST schema metadata. A constructor
    // call creates a typed map directly.
    std::unordered_map<SymbolId, std::shared_ptr<ClassStmt>> classDefinitions_;
    // Declarations reconstructed from RocksDB carry schema only. The first
    // matching source declaration replaces one so methods are loaded exactly
    // once; subsequent source declarations remain an error.
    std::unordered_set<std::string> persistedClassSchemas_;
    // `fields` is the declared class shape and is empty for an undeclared fact
    // type, which is not validated and only fixes its key field (the first field
    // of its first constructor).
    struct FactTypeContract {
        std::vector<std::string> fields;
        std::vector<std::string> keyFields;
        std::vector<std::vector<std::string>> indexes;
        bool declaredClass = false;
        // A declared class that extends an undeclared fact: the fields it inherits
        // from the parent fact are not fixed by any declaration, so a persisted
        // fact may carry fields beyond the declared ones.
        bool openShape = false;
    };
    std::unordered_map<std::string, FactTypeContract> factTypeContracts_;
    std::unique_ptr<RocksFactStore> durableStore_;
    std::unordered_map<std::string, SolveCacheEntry> solveCache_;
    std::list<std::string> solveCacheRecency_;
    std::size_t solveCacheBytes_ = 0;
    mutable std::unordered_map<const ClauseStmt*, MethodRuntimeInfo> methodRuntimeCache_;
    mutable std::unordered_map<std::string, ClauseList*> clauseLookupCache_;
    mutable std::unordered_map<SymbolId, std::vector<std::string>> typeAncestryCache_;
    mutable std::unordered_map<SymbolId,
        std::unordered_map<std::string, std::size_t>> typeAncestorDistanceCache_;
    mutable std::unordered_map<SymbolId, double> typeHierarchyDepthCache_;
    // These closures are valid only for one immutable hierarchy generation.
    // Fact mutations retain query indexes, but hierarchy changes must never
    // let a previous ancestry answer leak into a later analysis.
    mutable std::uint64_t ancestryCacheGeneration_ = 0;
    std::unordered_set<std::string> activeNegatedPredicates_;
    std::unordered_map<SymbolId, std::string> contraries_;
    std::unordered_map<SymbolId, std::shared_ptr<TableEvaluation>> tableCache_;
    std::set<std::filesystem::path> loadedFiles_;
    std::unordered_set<std::string> packageDiscoveryAttempts_;
    std::unordered_map<const ClauseStmt*, std::filesystem::path> clauseOrigins_;
    std::filesystem::path currentLoadingFile_;
    std::vector<NativeLibrary> nativeLibraries_;
    std::unordered_map<std::string, std::size_t> nativeLibraryByModule_;
    std::set<std::filesystem::path> nativeLibraryPaths_;
    std::unordered_map<std::string, std::shared_ptr<ThreadTask>> threadTasks_;
    mutable std::mutex threadMutex_;
    EnvFramePool envFramePool_;
    BindingTrail* activeBindingTrail_ = nullptr;
    size_t solveEpoch_ = 0;
    std::uint64_t programGeneration_ = 1;
    std::unordered_map<SymbolId, std::uint64_t> symbolGenerations_;
    size_t threadCounter_ = 0;
    size_t cacheInvalidationDepth_ = 0;
    bool pendingCacheInvalidation_ = false;
    bool strictValueFailures_ = false;
    bool valueCallMode_ = false;
    size_t valueCallTrampolineDepth_ = 0;
    size_t methodCallDepth_ = 0;
    // methodCallDepth_ when the innermost value-call trampoline started. A
    // `return call(...)` may jump through TailCallSignal only when it executes
    // in the body of the method that trampoline itself began (depth + 1); a
    // goal-position call nested deeper must evaluate its return normally.
    size_t trampolineMethodDepth_ = 0;
    std::vector<std::shared_ptr<Expr>> pipelineResults_;
    std::size_t clauseAttempts_ = 0;
    std::size_t unificationAttempts_ = 0;
    std::size_t factCandidates_ = 0;
    std::size_t solutionMaterializations_ = 0;
    std::size_t environmentCopies_ = 0;
    std::size_t standardizedClauses_ = 0;
    std::unordered_map<const ClauseStmt*, bool> clauseRenameRequirements_;
    std::size_t moduleLoads_ = 0;
    std::size_t nativeCalls_ = 0;
    std::size_t nativeFactProjectionCalls_ = 0;
    std::size_t nativeRequestBytes_ = 0;
    std::size_t nativeFactProjectionBytes_ = 0;
    std::size_t nativeSerializationMicros_ = 0;
    std::size_t streamedModuleMicros_ = 0;
    ParserMetrics parserMetrics_;
    std::size_t factRegistrationMicros_ = 0;
    GoalHook goalHook_;
    std::istream* inputStream_ = nullptr;
    std::ostream* outputStream_ = nullptr;
    mutable std::size_t dispatchCacheHits_ = 0;
    mutable std::size_t dispatchCacheMisses_ = 0;
    std::size_t tableCacheHits_ = 0;
    std::size_t tableCacheMisses_ = 0;
    std::size_t tableRounds_ = 0;
    std::size_t tableDeltaAnswers_ = 0;
    std::size_t provenanceNodes_ = 0;

    void solveRecursive(const std::vector<std::shared_ptr<Goal>>& goals,
                        Env env,
                        std::vector<Solution>& out,
                        size_t maxSolutions,
                        size_t depth);
    void solveIterative(const std::vector<std::shared_ptr<Goal>>& goals,
                        Env env,
                        std::vector<Solution>& out,
                        size_t maxSolutions,
                        size_t depth);
    bool solveAssignGoal(const AssignGoal& goal, Env& env);
    bool solveMultiAssignGoal(const MultiAssignGoal& goal, Env& env);
    bool solveBinaryGoal(const BinaryGoal& goal, Env& env);
    bool solveWhereGoal(const WhereGoal& goal, Env& env);
    bool solveReturnGoal(const ReturnGoal& goal, Env& env);
    bool solveNotGoal(const NotGoal& goal, Env& env, size_t depth);
    bool bodyHasReturnGoal(const std::vector<std::shared_ptr<Goal>>& goals) const;
    bool evaluateGoalTruth(const std::shared_ptr<Goal>& goal, Env& env);
    // Runs one statement's store mutations as a single durable transaction, or
    // inside the caller's when one is already open (a module load). Any
    // exception rolls the whole statement back.
    void withStoreTransaction(const std::function<void()>& work);
    // Truth tuple of a body that already solved: every goal succeeded, so no
    // goal is evaluated a second time.
    static std::shared_ptr<Expr> successTruthTuple(const std::vector<std::shared_ptr<Goal>>& goals);
    std::shared_ptr<Expr> executeGoalTruthTuple(const std::vector<std::shared_ptr<Goal>>& goals, Env env, Env& outEnv);
    bool solveMethodCall(const Call& call,
                         const std::shared_ptr<ClauseStmt>& clause,
                         Env env,
                         std::vector<Solution>& out,
                         size_t maxSolutions,
                         size_t depth,
                         const std::shared_ptr<Expr>& receiver = {});
    bool solveBuiltin(const Call& call, Env& env);
    bool solveNativeCall(const Call& call, Env& env);
    bool evalBuiltinTerm(const TermExpr& term, const Env& env, std::shared_ptr<Expr>& out);
    std::vector<const ClassFieldDecl*> classFieldsFor(const ClassStmt& schema) const;
    std::shared_ptr<MapExpr> nearestPrototypeValue(const std::string& type);
    bool instantiateClass(const TermExpr& term, const Env& env, std::shared_ptr<Expr>& out);
    bool evalDataValue(const std::shared_ptr<Expr>& expression,
                       const Env& env,
                       std::shared_ptr<Expr>& out);
    bool valueMatchesFieldType(const std::shared_ptr<Expr>& value,
                               const ClassFieldDecl::TypeRef& type) const;
    bool evalAncestorAnalysis(const Call& call, const Env& env, std::shared_ptr<Expr>& out);
    bool evalFactPropagation(const Call& call, const Env& env, std::shared_ptr<Expr>& out);
    bool isMethodTransitivelyPure(const std::shared_ptr<ClauseStmt>& clause,
                                  std::unordered_set<const ClauseStmt*>& visiting,
                                  std::string& reason) const;
    bool evalReasoningBuiltin(const TermExpr& term,
                              const Env& env,
                              std::shared_ptr<Expr>& out);
    bool evalArrayWherePredicate(const TermExpr& term,
                                 const Env& env,
                                 std::shared_ptr<Expr>& out);
    std::shared_ptr<MapExpr> prepareInsertedFact(const std::string& type,
                                                 const MapExpr& values,
                                                 const Env& env);
    void registerClassContract(const ClassStmt& declaration);
    // Validates a fact against its class or key contract before it is stored.
    void validateFactWrite(const std::string& type,
                           const std::shared_ptr<MapExpr>& value);
    std::vector<std::shared_ptr<Expr>> factKey(
        const std::string& type, const std::shared_ptr<MapExpr>& value) const;
    std::vector<StoredFactIndex> factIndexes(
        const std::string& type, const std::shared_ptr<MapExpr>& value) const;
    std::shared_ptr<MapExpr> publishFact(
        std::string type,
        std::string parentType,
        std::shared_ptr<MapExpr> value,
        std::filesystem::path origin = {},
        std::vector<std::uint64_t> parentFactIds = {},
        std::vector<SymbolId> designations = {},
        std::shared_ptr<MapExpr> temporalMetadata = {},
        bool idempotentSeed = false);
    std::shared_ptr<MapExpr> resolveLinkEndpoint(
        const std::shared_ptr<Expr>& expression,
        const Env& env);
    std::shared_ptr<MapExpr> publishLink(
        const std::vector<Arg>& arguments,
        const Env& env,
        bool idempotentSeed);
    std::shared_ptr<MapExpr> classGraphEdgeValue(const StoredClassEdge& edge) const;
    std::shared_ptr<MapExpr> createClassGraph(
        const std::vector<std::shared_ptr<Expr>>& endpoints);
    bool evalClassGraphMember(const std::shared_ptr<MapExpr>& graph,
                              const std::string& member,
                              const std::vector<Arg>& arguments,
                              const Env& env,
                              std::shared_ptr<Expr>& out);
    std::shared_ptr<ArrayExpr> insertFactsFromRows(const std::string& type,
                                                   const std::vector<std::shared_ptr<Expr>>& rows,
                                                   const std::filesystem::path& source);
    bool evalReasoningContrary(const TermExpr& term,
                               const Env& env,
                               std::shared_ptr<Expr>& out);
    bool evalReasoningProve(const TermExpr& term,
                            const Env& env,
                            std::shared_ptr<Expr>& out);
    bool evalCallAsValue(const TermExpr& term,
                         const Env& env,
                         std::shared_ptr<Expr>& out,
                         const std::shared_ptr<Expr>& receiver = {});
    bool evalCallAsValueOnce(const TermExpr& term,
                             const Env& env,
                             std::shared_ptr<Expr>& out,
                             const std::shared_ptr<Expr>& receiver = {});
    bool evalOperatorExpr(const OperatorExpression& expression,
                          const Env& env,
                          std::shared_ptr<Expr>& out);
    bool evalCustomOperatorExpr(const OperatorExpression& expression,
                                const Env& env,
                                std::shared_ptr<Expr>& out,
                                bool* matched = nullptr);
    bool evalExprValue(const std::shared_ptr<Expr>& expr, const Env& env, std::shared_ptr<Expr>& out);
    std::shared_ptr<Expr> executeEntryCall(const Call& entryCall);
    bool compareResolved(const std::shared_ptr<Expr>& left,
                         TokenId::Id op,
                         const std::shared_ptr<Expr>& right) const;

    bool unifyCall(const Call& goal, const Call& head, Env& env);
    std::vector<Env> unifyCallAlternatives(const Call& goal, const Call& head, const Env& env);
    bool unifyExpr(const std::shared_ptr<Expr>& a,
                   const std::shared_ptr<Expr>& b,
                   Env& env);

    const Arg* findArg(const Call& call, const Arg& wanted, size_t index) const;
    const Arg* findArgByNameOrIndex(const Call& call, const std::string& name, size_t index) const;
    ClauseList* findClauses(const std::string& name, SymbolId nameId);
    const ClauseList* findClauses(const std::string& name, SymbolId nameId) const;
    ClauseList& getOrCreateClauseList(const std::string& name, SymbolId nameId);
    void removeClauseBucket(const std::string& name, SymbolId nameId);
    void ensureClauseTableUnique();
    std::string solveCacheKey(const std::vector<std::shared_ptr<Goal>>& goals, size_t maxSolutions) const;
    std::size_t estimateCachedSolutionsBytes(const std::string& key,
                                             const std::vector<Solution>& solutions) const;
    void storeCachedSolutions(const std::string& key, const std::vector<Solution>& solutions);
    void invalidateCaches();
    void beginCacheInvalidationBatch();
    void endCacheInvalidationBatch();
    void clearCachesNow();

    void validateNegationStratification(const Program& program) const;
    bool isTableEligiblePredicate(const std::string& name,
                                  SymbolId nameId,
                                  std::unordered_set<SymbolId>& visiting,
                                  std::unordered_set<SymbolId>& checked,
                                  bool& recursive) const;
    bool isTableEligibleGoal(const std::shared_ptr<Goal>& goal,
                             SymbolId root,
                             std::unordered_set<SymbolId>& visiting,
                             std::unordered_set<SymbolId>& checked,
                             bool& recursive) const;
    std::shared_ptr<TableEvaluation> tableEvaluationFor(
        const std::string& name,
        SymbolId nameId,
        bool requireRecursive);
    std::shared_ptr<TableEvaluation> buildTableEvaluation(
        const std::string& name,
        SymbolId nameId);
    bool tableEvaluationValid(const TableEvaluation& evaluation) const;
    bool tableCallAnswers(const Call& call,
                          const Env& env,
                          std::vector<TableBinding>& bindings,
                          std::shared_ptr<TableEvaluation>* evaluation = nullptr,
                          bool requireRecursive = true);
    std::vector<TableBinding> evaluateTableGoals(
        const std::vector<std::shared_ptr<Goal>>& goals,
        std::vector<TableBinding> inputs,
        const TableEvaluation& evaluation,
        std::optional<std::pair<SymbolId, std::size_t>> deltaPivot);
    std::shared_ptr<MapExpr> materializeDerivationResult(
        const Call& query,
        const std::vector<TableBinding>& supporting,
        const std::vector<TableBinding>& opposing,
        const std::shared_ptr<TableEvaluation>& positiveEvaluation,
        const std::shared_ptr<TableEvaluation>& negativeEvaluation) const;

    std::shared_ptr<ClauseStmt> standardizeApart(const std::shared_ptr<ClauseStmt>& clause);
    Env copyExecutionEnvironment(const Env& source);
    bool exprNeedsRename(const std::shared_ptr<Expr>& expr) const;
    using RenameMap = std::unordered_map<SymbolId, SymbolId>;
    SymbolId renamedId(SymbolId original, RenameMap& names);
    Call renameCall(const Call& call, RenameMap& names);
    std::shared_ptr<Goal> renameGoal(const std::shared_ptr<Goal>& goal, RenameMap& names);
    std::shared_ptr<Expr> renameExpr(const std::shared_ptr<Expr>& expr, RenameMap& names);

    bool isSameVariable(const std::shared_ptr<Expr>& a, const std::shared_ptr<Expr>& b) const;
    bool isGroundLiteral(const std::shared_ptr<Expr>& expr) const;
    bool isCacheableQuery(const std::vector<std::shared_ptr<Goal>>& goals) const;
    bool goalMayHaveSideEffects(const std::shared_ptr<Goal>& goal) const;
    bool exprMayHaveSideEffects(const std::shared_ptr<Expr>& expr) const;
    bool isMethodClause(const ClauseStmt& clause) const;
    bool methodMetadataCacheEligible(const ClauseStmt& clause) const;
    MethodParamPlan makeMethodParamPlan(const Arg& param) const;
    std::vector<MethodParamPlan> buildMethodParamPlan(const ClauseStmt& clause) const;
    const std::vector<MethodParamPlan>* hotMethodParamPlan(const std::shared_ptr<ClauseStmt>& clause);
    struct FactMaterialization {
        std::shared_ptr<MapExpr> value;
        // Interpreter metadata is carried alongside a fact, never as part of
        // its public property map.
        std::shared_ptr<MapExpr> temporalMetadata;
        std::vector<std::uint64_t> parentFactIds;
    };
    FactMaterialization factToMap(const ClauseStmt& clause);
    std::vector<std::string> durableFactBuckets(const std::string& type) const;
    using FactSelectionVisitor =
        std::function<bool(const std::shared_ptr<MapExpr>&)>;
    std::shared_ptr<ArrayExpr> materializeFactSelection(
        const std::shared_ptr<Expr>& selection, std::size_t limit = 0,
        std::size_t* countOnly = nullptr,
        const FactSelectionVisitor* visitor = nullptr);
    std::size_t countFactSelection(const std::shared_ptr<Expr>& selection);
    std::shared_ptr<NumberExpr> aggregateFactSelection(
        const std::shared_ptr<Expr>& selection,
        const std::string& field,
        BuiltinId operation);
    using GraphSelectionVisitor =
        std::function<bool(const std::shared_ptr<MapExpr>&)>;
    std::shared_ptr<ArrayExpr> materializeGraphSelection(
        const std::shared_ptr<GraphSelectionExpr>& selection,
        std::size_t limit = 0,
        const GraphSelectionVisitor* visitor = nullptr);
    std::shared_ptr<Expr> materializeIfFactSelection(const std::shared_ptr<Expr>& value);
    void refreshAncestryCaches() const;
    const std::vector<std::string>& typeAncestry(const std::string& type) const;
    const std::unordered_map<std::string, std::size_t>& typeAncestorDistances(
        const std::string& type) const;
    double typeHierarchyDepth(const std::string& type) const;
    std::vector<std::shared_ptr<Expr>> valuesForLambdaSource(const std::shared_ptr<Expr>& source, const Env& env);

    bool ensurePredicateLoaded(const std::string& predicate);
    void loadNativeLibrary(const std::filesystem::path& file);
    void closeNativeLibraries();
    void joinThreads();
    std::shared_ptr<Expr> makeThreadHandle(const std::string& id) const;
    std::shared_ptr<ThreadTask> threadTaskFromHandle(const std::shared_ptr<Expr>& handle);
    std::string createThreadTask(const std::string& functionName);
    std::string startThreadTask(const std::shared_ptr<Expr>& handle);
    std::string threadTaskStatus(const std::shared_ptr<Expr>& handle);
    std::shared_ptr<Expr> threadTaskResult(const std::shared_ptr<Expr>& handle);
    void collectExecutionGarbage();
    const ClauseStmt* nativeDeclarationFor(const std::string& name) const;
    void validateNativeCallTypes(const Call& call,
                                 const ClauseStmt& declaration,
                                 const Env& env,
                                 bool requireDeclaredInputs = false);
    std::vector<std::filesystem::path> expandImportPattern(const std::filesystem::path& baseDir,
                                                           const std::string& pattern) const;
    std::filesystem::path resolveNativeImport(const std::filesystem::path& baseDir,
                                              const std::string& pattern) const;
};

} // namespace Felidae
