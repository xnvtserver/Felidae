#pragma once

#include "AST.h"
#include "Env.h"
#include "Memory.h"
#include "NativeRuntime.h"
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Felidae {

class InterpreterError : public std::runtime_error {
public:
    explicit InterpreterError(const std::string& msg) : std::runtime_error(msg) {}
};

class Interpreter {
public:
    ~Interpreter();

    void addProgram(const Program& program);
    void addClause(std::shared_ptr<ClauseStmt> clause);
    void addLazyImport(const std::filesystem::path& baseDir, const std::string& pattern);

    std::vector<Solution> solve(const std::vector<std::shared_ptr<Goal>>& queryGoals,
                                size_t maxSolutions = 1000);

    std::shared_ptr<Expr> resolveExpr(const std::shared_ptr<Expr>& expr, const Env& env) const;
    std::string exprToString(const std::shared_ptr<Expr>& expr, const Env& env) const;
    bool hasMethod(const std::string& name);
    bool hasGlobal(const std::string& name) const;
    std::shared_ptr<Expr> evaluateGlobal(const std::string& name) const;
    std::shared_ptr<Expr> evaluateExpressionText(const std::string& text);
    std::shared_ptr<Expr> callMain(const std::shared_ptr<Expr>& systemInput);
    std::string valueToString(const std::shared_ptr<Expr>& value) const;
    std::string runtimeGraphJson() const;
    std::string visualizeDataJson(bool loadImports = false);
    std::string visualizeDataHtml(bool loadImports = false);
    void loadAllImports();

private:
    struct LazyModule {
        std::filesystem::path baseDir;
        std::string pattern;
        bool loaded = false;
        size_t useCount = 0;
        size_t lastUsed = 0;
        std::vector<std::filesystem::path> files;
        std::filesystem::path nativeLibrary;
    };
    struct ThreadTask {
        explicit ThreadTask(std::string functionName) : functionName(std::move(functionName)) {}
        std::string functionName;
        std::thread worker;
        std::string status = "created";
        std::string result;
        std::string error;
        bool started = false;
    };

    std::unordered_map<std::string, std::vector<std::shared_ptr<ClauseStmt>>> clauses_;
    FactMemory memory_;
    std::unordered_map<std::string, std::shared_ptr<Expr>> globals_;
    std::unordered_map<std::string, std::vector<Solution>> solveCache_;
    std::vector<LazyModule> lazyModules_;
    std::set<std::filesystem::path> loadedFiles_;
    std::unordered_map<const ClauseStmt*, std::filesystem::path> clauseOrigins_;
    std::filesystem::path currentLoadingFile_;
    std::vector<NativeLibrary> nativeLibraries_;
    std::set<std::filesystem::path> nativeLibraryPaths_;
    std::unordered_map<std::string, std::shared_ptr<ThreadTask>> threadTasks_;
    mutable std::mutex threadMutex_;
    EnvFramePool envFramePool_;
    size_t solveEpoch_ = 0;
    size_t renameCounter_ = 0;
    size_t threadCounter_ = 0;
    bool strictValueFailures_ = false;
    bool valueCallMode_ = false;
    std::vector<std::shared_ptr<Expr>> pipelineResults_;

    void solveRecursive(const std::vector<std::shared_ptr<Goal>>& goals,
                        Env env,
                        std::vector<Solution>& out,
                        size_t maxSolutions,
                        size_t depth);
    void solveRecursiveFrame(const std::vector<std::shared_ptr<Goal>>& goals,
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
    bool evalCallAsValue(const TermExpr& term, const Env& env, std::shared_ptr<Expr>& out);
    bool evalPipelineExpr(const PipelineExpr& pipeline, const Env& env, std::shared_ptr<Expr>& out);
    bool evalExprValue(const std::shared_ptr<Expr>& expr, const Env& env, std::shared_ptr<Expr>& out);
    bool compareResolved(const std::shared_ptr<Expr>& left,
                         const std::string& op,
                         const std::shared_ptr<Expr>& right) const;

    bool unifyCall(const Call& goal, const Call& head, Env& env);
    std::vector<Env> unifyCallAlternatives(const Call& goal, const Call& head, const Env& env);
    bool unifyExpr(const std::shared_ptr<Expr>& a,
                   const std::shared_ptr<Expr>& b,
                   Env& env);

    const Arg* findArg(const Call& call, const Arg& wanted, size_t index) const;
    const Arg* findArgByNameOrIndex(const Call& call, const std::string& name, size_t index) const;
    std::string solveCacheKey(const std::vector<std::shared_ptr<Goal>>& goals, size_t maxSolutions) const;
    void invalidateCaches();

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
    std::shared_ptr<MapExpr> factToMap(const ClauseStmt& clause, const std::string& parentType);
    std::vector<std::shared_ptr<Expr>> valuesForLambdaSource(const std::shared_ptr<Expr>& source, const Env& env);

    bool ensurePredicateLoaded(const std::string& predicate);
    void touchClauses(const std::vector<std::shared_ptr<ClauseStmt>>& clauses);
    void evictColdModules();
    void loadLazyModule(LazyModule& module);
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
    void validateNativeCallTypes(const Call& call, const ClauseStmt& declaration, const Env& env);
    std::vector<std::filesystem::path> expandImportPattern(const std::filesystem::path& baseDir,
                                                           const std::string& pattern) const;
    std::filesystem::path resolveNativeImport(const std::filesystem::path& baseDir,
                                              const std::string& pattern) const;
};

} // namespace Felidae
