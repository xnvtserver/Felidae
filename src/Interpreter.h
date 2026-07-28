#pragma once

#include "AST.h"
#include "Env.h"
#include "Memory.h"
#include "NativeRuntime.h"
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
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
    void addClause(std::shared_ptr<ClauseStmt> clause);
    void addImport(const std::filesystem::path& baseDir, const std::string& pattern);

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
    std::string runtimeMetricsJson() const;
    std::size_t syncFactSource(const std::filesystem::path& file);

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
    struct ClauseBucket {
        std::string name;
        ClauseList clauses;
    };
    using ClauseTable = std::unordered_map<SymbolId, std::vector<ClauseBucket>>;

    std::shared_ptr<ClauseTable> clauses_ = std::make_shared<ClauseTable>();
    std::vector<Call> autoEntryCalls_;
    FactMemory memory_;
    GlobalEnv globals_;
    std::unordered_map<std::string, SolveCacheEntry> solveCache_;
    std::list<std::string> solveCacheRecency_;
    std::size_t solveCacheBytes_ = 0;
    mutable std::unordered_map<const ClauseStmt*, MethodRuntimeInfo> methodRuntimeCache_;
    mutable std::unordered_map<std::string, ClauseList*> clauseLookupCache_;
    mutable std::unordered_map<std::string, std::vector<std::string>> typeAncestryCache_;
    std::unordered_map<ComparisonDispatchKey,
                       ComparisonDispatchPlan,
                       ComparisonDispatchKeyHash> comparisonDispatchCache_;
    std::unordered_map<std::uint64_t, std::vector<ReferenceAttachment>> referencesBySource_;
    std::uint64_t nextReferenceAttachmentId_ = 1;
    std::size_t nextReferenceCreationOrder_ = 0;
    std::uint64_t referenceEvaluationGeneration_ = 0;
    std::unordered_set<std::string> activeReferenceEvaluations_;
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
    size_t solveEpoch_ = 0;
    size_t renameCounter_ = 0;
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
    std::size_t solutionMaterializations_ = 0;
    std::size_t standardizedClauses_ = 0;
    std::size_t moduleLoads_ = 0;
    std::size_t nativeCalls_ = 0;
    std::size_t nativeFactSnapshotCalls_ = 0;
    std::size_t nativeRequestBytes_ = 0;
    std::size_t nativeFactSnapshotBytes_ = 0;
    std::size_t nativeSerializationMicros_ = 0;
    mutable std::size_t dispatchCacheHits_ = 0;
    mutable std::size_t dispatchCacheMisses_ = 0;

    void solveRecursive(const std::vector<std::shared_ptr<Goal>>& goals,
                        Env env,
                        std::vector<Solution>& out,
                        size_t maxSolutions,
                        size_t depth);
    void solveRecursiveFrame(const std::vector<std::shared_ptr<Goal>>& goals,
                             size_t goalIndex,
                             Env& env,
                             std::vector<Solution>& out,
                             size_t maxSolutions,
                             size_t depth);

    bool solveAssignGoal(const AssignGoal& goal, Env& env);
    bool solveMultiAssignGoal(const MultiAssignGoal& goal, Env& env);
    bool solveBinaryGoal(const BinaryGoal& goal, Env& env);
    bool solveWhereGoal(const WhereGoal& goal, Env& env);
    bool solveReturnGoal(const ReturnGoal& goal, Env& env);
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
    bool evalRelationCompare(const Call& call, const Env& env, std::shared_ptr<Expr>& out);
    bool evalRelationFind(const Call& call, const Env& env, std::shared_ptr<Expr>& out);
    bool evalDependencySatisfied(const Call& call, const Env& env, std::shared_ptr<Expr>& out);
    bool evalFactReferences(const Call& call, const Env& env, std::shared_ptr<Expr>& out);
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
    bool evalPipelineExpr(const PipelineExpr& pipeline, const Env& env, std::shared_ptr<Expr>& out);
    bool evalExprValue(const std::shared_ptr<Expr>& expr, const Env& env, std::shared_ptr<Expr>& out);
    bool compareResolved(const std::shared_ptr<Expr>& left,
                         TokenType op,
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

    std::shared_ptr<ClauseStmt> standardizeApart(const ClauseStmt& clause);
    Call renameCall(const Call& call, const std::string& prefix);
    std::shared_ptr<Goal> renameGoal(const std::shared_ptr<Goal>& goal, const std::string& prefix);
    std::shared_ptr<Expr> renameExpr(const std::shared_ptr<Expr>& expr, const std::string& prefix);

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
    std::shared_ptr<MapExpr> factToMap(const ClauseStmt& clause, const std::string& parentType);
    std::shared_ptr<ArrayExpr> materializeFactSelection(const std::shared_ptr<Expr>& selection);
    const std::vector<std::string>& typeAncestry(const std::string& type) const;
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
