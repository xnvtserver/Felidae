#include "AstAnalyzer.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace Felidae {

namespace {

bool isIgnoredName(const std::string& name) {
    return name.empty() || name == "_" || name.rfind("__anon", 0) == 0 || name.rfind("__r", 0) == 0;
}

bool isBuiltinTypeName(const std::string& name) {
    return name == "string" || name == "number" || name == "int" || name == "float" ||
           name == "double" || name == "decimal" || name == "bool" || name == "array" ||
           name == "map" || name == "any";
}

bool isLikelyTypeName(const std::string& name) {
    return isBuiltinTypeName(name) ||
           (!name.empty() && std::isupper(static_cast<unsigned char>(name.front())));
}

void addVarUse(const std::string& name, std::set<std::string>& uses) {
    if (!isIgnoredName(name)) uses.insert(name);
}

void collectExprUses(const std::shared_ptr<Expr>& expr,
                     std::set<std::string>& vars,
                     std::set<std::string>& calls) {
    if (!expr) return;
    if (auto var = std::dynamic_pointer_cast<VarExpr>(expr)) {
        addVarUse(var->name, vars);
    } else if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
        calls.insert(term->name);
        if (term->name == "thread:createThread") {
            for (const auto& arg : term->args) {
                if (arg.name != "function" && arg.name != "name") continue;
                if (auto target = std::dynamic_pointer_cast<StringExpr>(arg.value)) calls.insert(target->value);
            }
        }
        for (const auto& arg : term->args) collectExprUses(arg.value, vars, calls);
    } else if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
        if (auto source = std::dynamic_pointer_cast<VarExpr>(lambda->source)) {
            if (isLikelyTypeName(source->name)) calls.insert(source->name);
        }
        collectExprUses(lambda->source, vars, calls);
        collectExprUses(lambda->body, vars, calls);
        if (lambda->right) collectExprUses(lambda->right, vars, calls);
    } else if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
        for (const auto& item : array->items) collectExprUses(item, vars, calls);
    } else if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
        for (const auto& entry : map->entries) collectExprUses(entry.value, vars, calls);
    } else if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
        collectExprUses(access->target, vars, calls);
    } else if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
        collectExprUses(binary->left, vars, calls);
        collectExprUses(binary->right, vars, calls);
    }
}

void collectGoalUses(const std::shared_ptr<Goal>& goal,
                     std::set<std::string>& vars,
                     std::set<std::string>& calls) {
    if (!goal) return;
    if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
            calls.insert(call->call.name);
        if (call->call.name == "thread:createThread") {
            for (const auto& arg : call->call.args) {
                if (arg.name != "function" && arg.name != "name") continue;
                if (auto target = std::dynamic_pointer_cast<StringExpr>(arg.value)) calls.insert(target->value);
            }
        }
        for (const auto& arg : call->call.args) collectExprUses(arg.value, vars, calls);
    } else if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
        if (assign->goal) collectGoalUses(assign->goal, vars, calls);
        if (assign->expr) collectExprUses(assign->expr, vars, calls);
    } else if (auto multi = std::dynamic_pointer_cast<MultiAssignGoal>(goal)) {
        collectExprUses(multi->expr, vars, calls);
    } else if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
        collectExprUses(binary->left, vars, calls);
        collectExprUses(binary->right, vars, calls);
    } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
        collectGoalUses(where->condition, vars, calls);
    } else if (auto ret = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
        for (const auto& field : ret->fields) collectExprUses(field.value, vars, calls);
    } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
        for (const auto& item : group->goals) collectGoalUses(item, vars, calls);
    } else if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
        for (const auto& branch : orGoal->branches) {
            for (const auto& item : branch) collectGoalUses(item, vars, calls);
        }
    }
}

void collectAssignedNames(const std::vector<std::shared_ptr<Goal>>& goals,
                          std::set<std::string>& assigned) {
    for (const auto& goal : goals) {
        if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
            if (!isIgnoredName(assign->name)) assigned.insert(assign->name);
            if (assign->goal) collectAssignedNames({assign->goal}, assigned);
        } else if (auto multi = std::dynamic_pointer_cast<MultiAssignGoal>(goal)) {
            for (const auto& target : multi->targets) {
                if (!isIgnoredName(target.name)) assigned.insert(target.name);
            }
        } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
            collectAssignedNames(group->goals, assigned);
        } else if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
            for (const auto& branch : orGoal->branches) collectAssignedNames(branch, assigned);
        } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
            collectAssignedNames({where->condition}, assigned);
        }
    }
}

void collectGlobalAssignmentCollisions(const std::vector<std::shared_ptr<Goal>>& goals,
                                       const std::set<std::string>& globals,
                                       std::vector<AstDiagnostic>& diagnostics) {
    for (const auto& goal : goals) {
        if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
            if (globals.count(assign->name)) {
                diagnostics.push_back(AstDiagnostic{
                    "error",
                    "Variable '" + assign->name + "' is already assigned and immutable.",
                    1,
                    1});
            }
            if (assign->goal) collectGlobalAssignmentCollisions({assign->goal}, globals, diagnostics);
        } else if (auto multi = std::dynamic_pointer_cast<MultiAssignGoal>(goal)) {
            for (const auto& target : multi->targets) {
                if (globals.count(target.name)) {
                    diagnostics.push_back(AstDiagnostic{
                        "error",
                        "Variable '" + target.name + "' is already assigned and immutable.",
                        1,
                        1});
                }
            }
        } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
            collectGlobalAssignmentCollisions(group->goals, globals, diagnostics);
        } else if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
            for (const auto& branch : orGoal->branches) {
                collectGlobalAssignmentCollisions(branch, globals, diagnostics);
            }
        } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
            collectGlobalAssignmentCollisions({where->condition}, globals, diagnostics);
        }
    }
}

std::string factSignature(const ClauseStmt& clause) {
    std::vector<std::string> fields;
    fields.reserve(clause.head.args.size());
    for (const auto& arg : clause.head.args) fields.push_back(arg.name + ":" + arg.value->debug());
    std::sort(fields.begin(), fields.end());
    std::ostringstream out;
    out << clause.head.name << "|";
    for (const auto& field : fields) out << field << "|";
    return out.str();
}

void warn(std::vector<AstDiagnostic>& diagnostics, std::string message) {
    diagnostics.push_back(AstDiagnostic{"warning", std::move(message), 1, 1});
}

} // namespace

std::vector<AstDiagnostic> analyzeProgramAst(const Program& program) {
    std::vector<AstDiagnostic> diagnostics;
    std::map<std::string, size_t> methodDefinitions;
    std::map<std::string, size_t> factDefinitions;
    std::set<std::string> globals;
    std::set<std::string> calls;
    std::set<std::string> factSignatures;

    for (const auto& stmt : program.statements) {
        if (auto binding = std::dynamic_pointer_cast<GlobalBindingStmt>(stmt)) {
            globals.insert(binding->name);
        }
    }

    for (const auto& stmt : program.statements) {
        if (auto binding = std::dynamic_pointer_cast<GlobalBindingStmt>(stmt)) {
            std::set<std::string> vars;
            collectExprUses(binding->expr, vars, calls);
            continue;
        }
        auto clause = std::dynamic_pointer_cast<ClauseStmt>(stmt);
        if (!clause) continue;

        if (clause->isFact()) {
            factDefinitions[clause->head.name]++;
            const auto signature = factSignature(*clause);
            if (!factSignatures.insert(signature).second) {
                warn(diagnostics, "Duplicate fact declaration for '" + clause->head.name + "'.");
            }
            continue;
        }

        methodDefinitions[clause->head.name]++;
        collectGlobalAssignmentCollisions(clause->body, globals, diagnostics);
        for (const auto& branch : clause->fallbackBranches) {
            collectGlobalAssignmentCollisions(branch, globals, diagnostics);
        }
        std::set<std::string> declared;
        std::set<std::string> used;
        for (const auto& arg : clause->head.args) {
            if (!arg.name.empty()) declared.insert(arg.name);
            if (auto var = std::dynamic_pointer_cast<VarExpr>(arg.value)) {
                if (!isIgnoredName(var->name) && !isLikelyTypeName(var->name)) declared.insert(var->name);
            }
        }
        collectAssignedNames(clause->body, declared);
        for (const auto& branch : clause->fallbackBranches) collectAssignedNames(branch, declared);
        for (const auto& goal : clause->body) collectGoalUses(goal, used, calls);
        for (const auto& branch : clause->fallbackBranches) {
            for (const auto& goal : branch) collectGoalUses(goal, used, calls);
        }
        for (const auto& name : declared) {
            if (!used.count(name)) warn(diagnostics, "Variable '" + name + "' is declared but never used in method '" + clause->head.name + "'.");
        }
    }

    for (const auto& item : methodDefinitions) {
        if (item.first != "main" && !calls.count(item.first)) {
            warn(diagnostics, "Method '" + item.first + "' is defined but not called in this file.");
        }
    }
    for (const auto& item : globals) {
        if (!calls.count(item)) {
            warn(diagnostics, "Global '" + item + "' is defined but not referenced in this file.");
        }
    }
    for (const auto& item : factDefinitions) {
        if (!calls.count(item.first)) {
            warn(diagnostics, "Fact type '" + item.first + "' is declared but not referenced in this file.");
        }
    }

    return diagnostics;
}

} // namespace Felidae
