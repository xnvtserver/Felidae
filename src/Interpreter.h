#pragma once

#include "AST.h"
#include "Env.h"
#include "Memory.h"
#include "NativeRuntime.h"
#include "ParserMetrics.h"
#include <filesystem>
#include <functional>
#include <list>
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

class InterpreterError : public std::runtime_error {
public:
    explicit InterpreterError(const std::string& msg) : std::runtime_error(msg) {}
};

class Interpreter {
public:
    using ClauseList = std::vector<std::shared_ptr<ClauseStmt>>;

    ~Interpreter();

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

    // `exhaustive`, when given, is set to whether `queryGoals` was searched
    // to completion (every solution found) rather than cut off at
    // maxSolutions - the search space still had untried alternatives left
    // when it stopped. A caller that needs to know rather than assume its
    // answer set is complete (Reasoning.prove's certificate, a query tool
    // reporting truncation) passes this instead of trusting silence.
    std::vector<Solution> solve(const std::vector<std::shared_ptr<Goal>>& queryGoals,
                                size_t maxSolutions = 1000,
                                bool* exhaustive = nullptr);

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
    std::string runtimeMetricsJson() const;
    void recordStreamedModuleMicros(std::size_t micros);
    void recordParserMetrics(const ParserMetrics& metrics);
    std::shared_ptr<OperatorRegistry> operatorRegistry() const { return operators_; }
    // Imported source files registered by the current interpreter.  The root
    // module is owned by the frontend; callers add it to any watch set.
    std::vector<std::filesystem::path> loadedSourceFiles() const;

    // Source analysis needs the real import resolver and operator registry,
    // but must never run entry calls or evaluate global initializers. Set this
    // before loading any program; normal interpreters keep the default.
    void setLoadEvaluationEnabled(bool enabled) { loadEvaluationEnabled_ = enabled; }
    using StatementLoadHook =
        std::function<void(const std::shared_ptr<Statement>& statement)>;
    void setStatementLoadHook(StatementLoadHook hook) {
        statementLoadHook_ = std::move(hook);
    }

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
        // Whether `solutions` is every solution (search space fully explored)
        // or was cut off by maxSolutions - cached alongside the answers
        // themselves so a cache hit doesn't lose the completeness the
        // original solve() call established.
        bool exhaustive = true;
    };
    struct ComparisonDispatchKey {
        SymbolId sourceTypeId = 0;
        SymbolId targetTypeId = 0;
        std::string sourceType;
        std::string targetType;

        bool operator==(const ComparisonDispatchKey& other) const {
            return sourceTypeId == other.sourceTypeId && targetTypeId == other.targetTypeId &&
                   sourceType == other.sourceType && targetType == other.targetType;
        }
    };
    struct ComparisonDispatchKeyHash {
        std::size_t operator()(const ComparisonDispatchKey& key) const {
            std::size_t seed = std::hash<SymbolId>{}(key.sourceTypeId);
            seed ^= std::hash<SymbolId>{}(key.targetTypeId) + 0x9e3779b9U +
                    (seed << 6U) + (seed >> 2U);
            seed ^= std::hash<std::string>{}(key.sourceType) + 0x9e3779b9U +
                    (seed << 6U) + (seed >> 2U);
            return seed ^ (std::hash<std::string>{}(key.targetType) + 0x9e3779b9U +
                           (seed << 6U) + (seed >> 2U));
        }
    };
    struct ComparisonDispatchPlan {
        std::shared_ptr<ClauseStmt> membershipClause;
        std::shared_ptr<ClauseStmt> comparisonClause;
        std::string membershipName;
        std::string comparisonName;
        std::string targetFamily;
    };
    struct ReferenceAttachment {
        std::uint64_t id = 0;
        std::uint64_t sourceFactId = 0;
        std::string callableName;
        std::shared_ptr<ClauseStmt> callable;
        std::shared_ptr<Expr> defaultFactor;
        std::shared_ptr<MapExpr> descriptor;
        std::size_t creationOrder = 0;
        std::shared_ptr<MapExpr> canonicalResult;
        std::uint64_t canonicalGeneration = 0;
        bool dirty = true;
    };
    // Keyed by SymbolId alone: SymbolInterner (Symbol.h) already guarantees a
    // collision-free, bijective id per distinct spelling, so a name-checked
    // bucket list under each id could never hold more than one entry - that
    // extra layer used to duplicate, with strings, the uniqueness the
    // interner already owns as the single source of truth for identity.
    using ClauseTable = std::unordered_map<SymbolId, ClauseList>;

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
        std::unordered_map<SymbolId, std::uint64_t> relationGenerations;
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
        FactMemory memory;
        GlobalEnv globals;
        std::unordered_map<SymbolId, std::shared_ptr<ClassStmt>> classDefinitions;
        std::unordered_map<std::uint64_t, std::vector<ReferenceAttachment>> referencesBySource;
        std::uint64_t nextReferenceAttachmentId = 1;
        std::size_t nextReferenceCreationOrder = 0;
        std::uint64_t referenceEvaluationGeneration = 0;
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
    std::unordered_map<PatternId, std::vector<std::shared_ptr<ClauseStmt>>> operatorClauses_;
    std::unique_ptr<ModuleTransactionState> moduleTransaction_;
    std::vector<Call> autoEntryCalls_;
    std::vector<std::shared_ptr<Expr>> autoEntryResults_;
    FactMemory memory_;
    GlobalEnv globals_;
    // Class declarations are retained as AST schema metadata.  A constructor
    // call creates a typed map directly; it never passes through IR or a VM.
    std::unordered_map<SymbolId, std::shared_ptr<ClassStmt>> classDefinitions_;
    std::unordered_map<std::string, SolveCacheEntry> solveCache_;
    std::list<std::string> solveCacheRecency_;
    std::size_t solveCacheBytes_ = 0;
    mutable std::unordered_map<const ClauseStmt*, MethodRuntimeInfo> methodRuntimeCache_;
    // Keyed by SymbolId, not name: every lookup here is on the hottest
    // dispatch path in the interpreter (every call), and a SymbolId hashes
    // in O(1) where a variable-length name hashes in O(len) - the cache
    // itself must not be the one place a fast, ID-based dispatch design
    // still pays a string cost on every hit.
    mutable std::unordered_map<SymbolId, ClauseList*> clauseLookupCache_;
    // Set (never cleared mid-search) whenever any level of solveIterative -
    // the top-level query or a nested method-call sub-solve - stops because
    // it hit its maxSolutions budget while alternatives remained, rather
    // than because the search space was actually exhausted. solve() resets
    // this before it starts and reads it back when done, so a truncation
    // anywhere in a nested solve is never lost by the time it reaches the
    // caller that asked whether the answer set is complete.
    bool searchTruncated_ = false;
    mutable std::unordered_map<SymbolId, std::vector<std::string>> typeAncestryCache_;
    mutable std::unordered_map<SymbolId,
        std::unordered_map<std::string, std::size_t>> typeAncestorDistanceCache_;
    mutable std::unordered_map<SymbolId, double> typeHierarchyDepthCache_;
    // These closures are valid only for one immutable hierarchy generation.
    // Fact mutations retain query indexes, but hierarchy changes must never
    // let a previous ancestry answer leak into a later analysis.
    mutable std::uint64_t ancestryCacheGeneration_ = 0;
    std::unordered_map<ComparisonDispatchKey,
                       ComparisonDispatchPlan,
                       ComparisonDispatchKeyHash> comparisonDispatchCache_;
    std::unordered_map<std::uint64_t, std::vector<ReferenceAttachment>> referencesBySource_;
    std::uint64_t nextReferenceAttachmentId_ = 1;
    std::size_t nextReferenceCreationOrder_ = 0;
    std::uint64_t referenceEvaluationGeneration_ = 0;
    std::unordered_set<std::string> activeReferenceEvaluations_;
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
    std::unordered_set<std::string> activeComparisons_;
    std::vector<std::shared_ptr<Expr>> pipelineResults_;
    std::size_t clauseAttempts_ = 0;
    std::size_t unificationAttempts_ = 0;
    std::size_t factCandidates_ = 0;
    std::size_t relationshipCandidates_ = 0;
    std::size_t relationshipCandidatesPruned_ = 0;
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
    bool loadEvaluationEnabled_ = true;
    StatementLoadHook statementLoadHook_;
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
    std::shared_ptr<Expr> evaluateGoalTruthTuple(const std::vector<std::shared_ptr<Goal>>& goals, Env env);
    std::shared_ptr<Expr> executeGoalTruthTuple(const std::vector<std::shared_ptr<Goal>>& goals, Env env, Env& outEnv);
    bool solveMethodCall(const Call& call,
                         const std::shared_ptr<ClauseStmt>& clause,
                         Env env,
                         std::vector<Solution>& out,
                         size_t maxSolutions,
                         size_t depth);
    bool solveBuiltin(const Call& call, Env& env);
    bool solveNativeCall(const Call& call, Env& env);
    bool evalBuiltinTerm(const TermExpr& term, const Env& env, std::shared_ptr<Expr>& out);
    std::vector<const ClassFieldDecl*> classFieldsFor(const ClassStmt& schema) const;
    std::optional<std::size_t> nearestPrototypeFact(const std::string& type);
    bool instantiateClass(const TermExpr& term, const Env& env, std::shared_ptr<Expr>& out);
    bool evalAncestorAnalysis(const Call& call, const Env& env, std::shared_ptr<Expr>& out);
    bool evalFactPropagation(const Call& call, const Env& env, std::shared_ptr<Expr>& out);
    bool evalRelationCompare(const Call& call, const Env& env, std::shared_ptr<Expr>& out);
    bool evalRelationFind(const Call& call, const Env& env, std::shared_ptr<Expr>& out);
    bool evalDependencySatisfied(const Call& call, const Env& env, std::shared_ptr<Expr>& out);
    bool evalFactReferences(const Call& call, const Env& env, std::shared_ptr<Expr>& out);
    bool evalReasoningBuiltin(const TermExpr& term,
                              const Env& env,
                              std::shared_ptr<Expr>& out);
    bool evalArrayWherePredicate(const TermExpr& term,
                                 const Env& env,
                                 std::shared_ptr<Expr>& out);
    std::shared_ptr<MapExpr> prepareInsertedFact(const std::string& type,
                                                 const MapExpr& values,
                                                 const Env& env);
    std::shared_ptr<ArrayExpr> insertFactsFromRows(const std::string& type,
                                                   const std::vector<std::shared_ptr<Expr>>& rows,
                                                   const std::filesystem::path& source);
    bool evalReasoningContrary(const TermExpr& term,
                               const Env& env,
                               std::shared_ptr<Expr>& out);
    bool evalReasoningProve(const TermExpr& term,
                            const Env& env,
                            std::shared_ptr<Expr>& out);
    bool evalReasoningGrade(const TermExpr& term,
                            const Env& env,
                            std::shared_ptr<Expr>& out,
                            const std::shared_ptr<MapExpr>& exact = {});
    bool solveFactAttachment(const Call& call, Env& env);
    bool attachFactReference(const Call& call, Env& env, std::uint64_t sourceFactId);
    std::shared_ptr<ClauseStmt> resolveReferenceCallable(const std::shared_ptr<Expr>& callable,
                                                          const std::shared_ptr<Expr>& source,
                                                          const std::shared_ptr<Expr>& factor,
                                                          std::string& normalizedName);
    bool validateReferenceResult(const std::shared_ptr<Expr>& value,
                                 std::shared_ptr<MapExpr>& result) const;
    bool isReferenceMethodPure(const std::shared_ptr<ClauseStmt>& clause,
                               std::unordered_set<const ClauseStmt*>& visiting,
                               std::string& reason) const;
    bool referenceValueMatchesType(const std::shared_ptr<Expr>& value,
                                   const MethodParamPlan& parameter) const;
    std::shared_ptr<Expr> referenceEffectiveFactor(const ReferenceAttachment& attachment) const;
    bool invokeComparisonMethod(const std::shared_ptr<ClauseStmt>& clause,
                                const Call& call,
                                const Env& env,
                                std::shared_ptr<Expr>& out);
    bool evalCallAsValue(const TermExpr& term, const Env& env, std::shared_ptr<Expr>& out);
    bool evalCallAsValueOnce(const TermExpr& term, const Env& env, std::shared_ptr<Expr>& out);
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
    void ensureClauseTableUnique();
    std::string solveCacheKey(const std::vector<std::shared_ptr<Goal>>& goals, size_t maxSolutions) const;
    std::size_t estimateCachedSolutionsBytes(const std::string& key,
                                             const std::vector<Solution>& solutions) const;
    void storeCachedSolutions(const std::string& key, const std::vector<Solution>& solutions,
                              bool exhaustive);
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
    std::shared_ptr<FactSelectionExpr> makeFactSelection(
        const std::string& type,
        const std::shared_ptr<MapExpr>& match = {});
    std::shared_ptr<ArrayExpr> projectFacts(
        const std::shared_ptr<FactSelectionExpr>& selection,
        const ArrayExpr& fields);
    double aggregateFacts(const std::shared_ptr<FactSelectionExpr>& selection,
                          const std::string& field,
                          std::uint8_t operation);
    std::shared_ptr<ArrayExpr> searchFacts(
        const std::shared_ptr<FactSelectionExpr>& selection,
        const std::string& field,
        const MapExpr& options);
    std::shared_ptr<ArrayExpr> joinFacts(const std::string& leftType,
                                         const std::string& rightType,
                                         const std::string& leftField,
                                         const std::string& rightField,
                                         std::uint8_t kind);
    std::shared_ptr<MapExpr> insertFact(
        const std::string& type,
        const MapExpr& values,
        const Env& env,
        const std::optional<std::filesystem::path>& source);
    std::shared_ptr<ArrayExpr> updateFacts(
        const std::shared_ptr<FactSelectionExpr>& selection,
        const MapExpr& values);
    double deleteFacts(const std::shared_ptr<FactSelectionExpr>& selection);
    void persistFactSource(
        const std::filesystem::path& source,
        const std::shared_ptr<MapExpr>& emptySchema = {});
    static bool exprEqualsLiteral(const std::shared_ptr<Expr>& left,
                                  const std::shared_ptr<Expr>& right);
    static bool exprContainsLiteral(const std::shared_ptr<Expr>& value,
                                    const std::shared_ptr<Expr>& expected);
    std::shared_ptr<ArrayExpr> materializeFactSelection(
        const std::shared_ptr<Expr>& selection,
        std::optional<std::size_t> maximumRows = std::nullopt);
    std::shared_ptr<Expr> materializeIfFactSelection(const std::shared_ptr<Expr>& value);
    void refreshAncestryCaches() const;
    const std::vector<std::string>& typeAncestry(const std::string& type) const;
    const std::unordered_map<std::string, std::size_t>& typeAncestorDistances(
        const std::string& type) const;
    double typeHierarchyDepth(const std::string& type) const;
    std::vector<std::shared_ptr<Expr>> valuesForLambdaSource(const std::shared_ptr<Expr>& source, const Env& env);

    bool ensurePredicateLoaded(const std::string& predicate);
    void loadProgramFile(const std::filesystem::path& file);
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
