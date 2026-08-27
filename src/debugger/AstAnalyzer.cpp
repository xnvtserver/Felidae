#include "debugger/AstAnalyzer.h"
#include "Symbol.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <unordered_set>

namespace Felidae {

namespace {

bool isIgnoredName(const std::string &name) {
  return name.empty() || name == "_" ||
         name == internalSymbolName(InternalSymbolKind::SystemResult);
}

bool isLikelyTypeName(const std::string &name) {
  return isFelidaeLikelyTypeName(name) || name == "map";
}

// Head parameters in the shape editor integrations want for signature help.
// A head writes either an annotation (`name: string`, `input: Person`) or a
// plain binding (`employee: e`); only the former has a type worth showing,
// which is exactly the distinction isLikelyTypeName already draws for the
// unused-variable diagnostics below. A literal default contributes its own
// kind as the type.
std::vector<SymbolParameter> collectHeadParameters(const Call &head) {
  std::vector<SymbolParameter> params;
  params.reserve(head.args.size());
  for (const auto &arg : head.args) {
    SymbolParameter param;
    param.name = arg.name;
    if (auto var = std::dynamic_pointer_cast<VarExpr>(arg.value)) {
      if (isLikelyTypeName(var->name)) {
        param.type = var->name;
      } else if (param.name.empty()) {
        // Positional head argument: the bound variable is the only
        // name this parameter has.
        param.name = var->name;
      }
    } else if (std::dynamic_pointer_cast<StringExpr>(arg.value)) {
      param.type = "string";
    } else if (std::dynamic_pointer_cast<NumberExpr>(arg.value)) {
      param.type = "number";
    } else if (std::dynamic_pointer_cast<BoolExpr>(arg.value)) {
      param.type = "bool";
    }
    if (param.name.empty())
      continue;
    params.push_back(std::move(param));
  }
  return params;
}

void addVarUse(const std::string &name, std::set<std::string> &uses) {
  if (!isIgnoredName(name))
    uses.insert(name);
}

bool hasSuffix(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

void collectReferenceCallable(const Call &call, std::set<std::string> &calls) {
  if (!hasSuffix(call.name, ":references"))
    return;
  calls.insert("Fact:references");
  for (const auto &arg : call.args) {
    if (arg.name != "by")
      continue;
    const auto callable = std::dynamic_pointer_cast<VarExpr>(arg.value);
    if (!callable)
      continue;
    std::string name = callable->name;
    const size_t separator = name.find("::");
    if (separator != std::string::npos)
      name.replace(separator, 2, ":");
    calls.insert(std::move(name));
  }
}

void collectExprUses(const std::shared_ptr<Expr> &expr,
                     std::set<std::string> &vars,
                     std::set<std::string> &calls) {
  if (!expr)
    return;
  if (auto var = std::dynamic_pointer_cast<VarExpr>(expr)) {
    addVarUse(var->name, vars);
  } else if (auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
    calls.insert(term->name);
    if (term->name == "Reasoning:contrary") {
      for (const auto &arg : term->args) {
        if (arg.name != "positive" && arg.name != "negative")
          continue;
        if (auto predicate = std::dynamic_pointer_cast<StringExpr>(arg.value)) {
          calls.insert(predicate->value);
        }
      }
    }
    if (term->name == "thread:createThread") {
      for (const auto &arg : term->args) {
        if (arg.name != "function" && arg.name != "name")
          continue;
        if (auto target = std::dynamic_pointer_cast<StringExpr>(arg.value))
          calls.insert(target->value);
      }
    }
    for (const auto &arg : term->args)
      collectExprUses(arg.value, vars, calls);
  } else if (auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
    if (auto source = std::dynamic_pointer_cast<VarExpr>(lambda->source)) {
      if (isLikelyTypeName(source->name))
        calls.insert(source->name);
    }
    collectExprUses(lambda->source, vars, calls);
    collectExprUses(lambda->body, vars, calls);
    if (lambda->right)
      collectExprUses(lambda->right, vars, calls);
  } else if (auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
    for (const auto &item : array->items)
      collectExprUses(item, vars, calls);
  } else if (auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
    for (const auto &entry : map->entries)
      collectExprUses(entry.value, vars, calls);
  } else if (auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
    auto targetVar = std::dynamic_pointer_cast<VarExpr>(access->target);
    if (access->key == "result" && targetVar && targetVar->name == "system")
      return;
    collectExprUses(access->target, vars, calls);
  } else if (auto op = std::dynamic_pointer_cast<OperatorExpression>(expr)) {
    for (size_t i = 0; i < op->captureCount(); ++i)
      collectExprUses(op->capture(i), vars, calls);
  }
}

void collectGoalUses(const std::shared_ptr<Goal> &goal,
                     std::set<std::string> &vars,
                     std::set<std::string> &calls) {
  if (!goal)
    return;
  if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
    calls.insert(call->call.name);
    collectReferenceCallable(call->call, calls);
    if (call->call.name == "thread:createThread") {
      for (const auto &arg : call->call.args) {
        if (arg.name != "function" && arg.name != "name")
          continue;
        if (auto target = std::dynamic_pointer_cast<StringExpr>(arg.value))
          calls.insert(target->value);
      }
    }
    for (const auto &arg : call->call.args)
      collectExprUses(arg.value, vars, calls);
  } else if (auto notGoal = std::dynamic_pointer_cast<NotGoal>(goal)) {
    calls.insert(notGoal->call.name);
    for (const auto &arg : notGoal->call.args)
      collectExprUses(arg.value, vars, calls);
  } else if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
    if (assign->expr)
      collectExprUses(assign->expr, vars, calls);
  } else if (auto multi = std::dynamic_pointer_cast<MultiAssignGoal>(goal)) {
    collectExprUses(multi->expr, vars, calls);
  } else if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
    collectExprUses(binary->left, vars, calls);
    collectExprUses(binary->right, vars, calls);
  } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
    collectGoalUses(where->condition, vars, calls);
  } else if (auto ifGoal = std::dynamic_pointer_cast<IfGoal>(goal)) {
    collectGoalUses(ifGoal->condition, vars, calls);
    for (const auto &item : ifGoal->thenBranch)
      collectGoalUses(item, vars, calls);
    for (const auto &item : ifGoal->elseBranch)
      collectGoalUses(item, vars, calls);
  } else if (auto ret = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
    for (const auto &field : ret->fields)
      collectExprUses(field.value, vars, calls);
  } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
    for (const auto &item : group->goals)
      collectGoalUses(item, vars, calls);
  } else if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
    for (const auto &branch : orGoal->branches) {
      for (const auto &item : branch)
        collectGoalUses(item, vars, calls);
    }
  }
}

void collectAssignedNames(const std::vector<std::shared_ptr<Goal>> &goals,
                          std::set<std::string> &assigned) {
  for (const auto &goal : goals) {
    if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
      if (!isIgnoredName(assign->name))
        assigned.insert(assign->name);
    } else if (auto multi = std::dynamic_pointer_cast<MultiAssignGoal>(goal)) {
      for (const auto &target : multi->targets) {
        if (!isIgnoredName(target.name))
          assigned.insert(target.name);
      }
    } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
      collectAssignedNames(group->goals, assigned);
    } else if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
      for (const auto &branch : orGoal->branches)
        collectAssignedNames(branch, assigned);
    } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
      collectAssignedNames({where->condition}, assigned);
    } else if (auto ifGoal = std::dynamic_pointer_cast<IfGoal>(goal)) {
      collectAssignedNames({ifGoal->condition}, assigned);
      collectAssignedNames(ifGoal->thenBranch, assigned);
      collectAssignedNames(ifGoal->elseBranch, assigned);
    }
  }
}

AstDiagnostic diagnosticFor(const AstNode *node, std::string severity,
                            std::string code, std::string message) {
  SourceSpan span;
  if (node && node->sourceSpan.valid())
    span = node->sourceSpan;
  return AstDiagnostic{std::move(severity), std::move(message),
                       span.startLine,      span.startColumn,
                       span.endLine,        span.endColumn,
                       std::move(code),     ""};
}

AstDiagnostic diagnosticForSpan(const SourceSpan &span, std::string severity,
                                std::string code, std::string message) {
  return AstDiagnostic{std::move(severity), std::move(message),
                       span.startLine,      span.startColumn,
                       span.endLine,        span.endColumn,
                       std::move(code),     ""};
}

void collectGlobalAssignmentCollisions(
    const std::vector<std::shared_ptr<Goal>> &goals,
    const std::set<std::string> &globals,
    std::vector<AstDiagnostic> &diagnostics) {
  for (const auto &goal : goals) {
    if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
      if (globals.count(assign->name)) {
        diagnostics.push_back(diagnosticFor(
            assign.get(), "error", "felidae.immutable.global_reassignment",
            "Variable '" + assign->name +
                "' is already assigned and immutable."));
      }
    } else if (auto multi = std::dynamic_pointer_cast<MultiAssignGoal>(goal)) {
      for (const auto &target : multi->targets) {
        if (globals.count(target.name)) {
          diagnostics.push_back(diagnosticFor(
              multi.get(), "error", "felidae.immutable.global_reassignment",
              "Variable '" + target.name +
                  "' is already assigned and immutable."));
        }
      }
    } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
      collectGlobalAssignmentCollisions(group->goals, globals, diagnostics);
    } else if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
      for (const auto &branch : orGoal->branches) {
        collectGlobalAssignmentCollisions(branch, globals, diagnostics);
      }
    } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
      collectGlobalAssignmentCollisions({where->condition}, globals,
                                        diagnostics);
    } else if (auto ifGoal = std::dynamic_pointer_cast<IfGoal>(goal)) {
      collectGlobalAssignmentCollisions({ifGoal->condition}, globals,
                                        diagnostics);
      collectGlobalAssignmentCollisions(ifGoal->thenBranch, globals,
                                        diagnostics);
      collectGlobalAssignmentCollisions(ifGoal->elseBranch, globals,
                                        diagnostics);
    }
  }
}

std::string factSignature(const ClauseStmt &clause) {
  std::vector<std::string> fields;
  fields.reserve(clause.head.args.size());
  for (const auto &arg : clause.head.args)
    fields.push_back(arg.name + ":" + arg.value->debug());
  std::sort(fields.begin(), fields.end());
  std::ostringstream out;
  out << clause.head.name << "|";
  for (const auto &field : fields)
    out << field << "|";
  return out.str();
}

void warn(std::vector<AstDiagnostic> &diagnostics, const AstNode *node,
          std::string code, std::string message) {
  diagnostics.push_back(
      diagnosticFor(node, "warning", std::move(code), std::move(message)));
}

void collectDiscardedExpressions(
    const std::vector<std::shared_ptr<Goal>> &goals, const std::string &method,
    std::vector<AstDiagnostic> &diagnostics) {
  for (const auto &goal : goals) {
    if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
      warn(diagnostics, binary.get(), "felidae.expression.discarded",
           "Raw expression '" + binary->debug() + "' in method '" + method +
               "' has no result consumer. Use it in where/if, assign or return "
               "it, "
               "or pass it to a call such as system.print.");
    } else if (auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
      collectDiscardedExpressions(group->goals, method, diagnostics);
    } else if (auto orGoal = std::dynamic_pointer_cast<OrGoal>(goal)) {
      for (const auto &branch : orGoal->branches) {
        collectDiscardedExpressions(branch, method, diagnostics);
      }
    } else if (auto ifGoal = std::dynamic_pointer_cast<IfGoal>(goal)) {
      // The condition is consumed by if. Branch bodies still need checking.
      collectDiscardedExpressions(ifGoal->thenBranch, method, diagnostics);
      collectDiscardedExpressions(ifGoal->elseBranch, method, diagnostics);
    } else if (auto assign = std::dynamic_pointer_cast<AssignGoal>(goal)) {
      // Assignment consumes either its expression or nested goal result.
      (void)assign;
    } else if (auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
      // The condition is consumed by where.
      (void)where;
    }
  }
}

void validateReasoningExpr(const std::shared_ptr<Expr> &expr,
                           const AstNode *owner,
                           std::vector<AstDiagnostic> &diagnostics) {
  if (!expr)
    return;
  if (const auto term = std::dynamic_pointer_cast<TermExpr>(expr)) {
    if (term->name == "Evidence" || term->name == "FuzzyMembership") {
      for (const auto &argument : term->args) {
        if (argument.name == "probability" || argument.name == "similarity") {
          diagnostics.push_back(diagnosticFor(
              owner, "error", "felidae.reasoning.dimension_conflation",
              term->name + " uses '" + argument.name +
                  "' as membership evidence. Keep probability, "
                  "similarity, and fuzzy degree in separate fields."));
        }
        if (argument.name != "degree" && argument.name != "reliability")
          continue;
        const auto number =
            std::dynamic_pointer_cast<NumberExpr>(argument.value);
        if (number && (!std::isfinite(number->value) || number->value < 0.0 ||
                       number->value > 1.0)) {
          diagnostics.push_back(
              diagnosticFor(owner, "error", "felidae.reasoning.invalid_grade",
                            term->name + " '" + argument.name +
                                "' must be between 0 and 1."));
        }
      }
    } else if (term->name == "Fact:materialize" ||
               term->name == "db:materialize") {
      diagnostics.push_back(diagnosticFor(
          owner, "warning", "felidae.selection.hidden_materialization",
          "Materializing a FactSelection here creates an eager array; "
          "keep the selection lazy unless an interop boundary requires it."));
    }
    for (const auto &argument : term->args) {
      validateReasoningExpr(argument.value, owner, diagnostics);
    }
    return;
  }
  if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
    for (const auto &item : array->items) {
      validateReasoningExpr(item, owner, diagnostics);
    }
  } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(expr)) {
    for (const auto &entry : map->entries) {
      validateReasoningExpr(entry.value, owner, diagnostics);
    }
  } else if (const auto op =
                 std::dynamic_pointer_cast<OperatorExpression>(expr)) {
    for (size_t i = 0; i < op->captureCount(); ++i) {
      validateReasoningExpr(op->capture(i), owner, diagnostics);
    }
  } else if (const auto access = std::dynamic_pointer_cast<AccessExpr>(expr)) {
    validateReasoningExpr(access->target, owner, diagnostics);
  } else if (const auto lambda = std::dynamic_pointer_cast<LambdaExpr>(expr)) {
    validateReasoningExpr(lambda->source, owner, diagnostics);
    validateReasoningExpr(lambda->body, owner, diagnostics);
    validateReasoningExpr(lambda->right, owner, diagnostics);
  }
}

void validateReasoningGoals(const std::vector<std::shared_ptr<Goal>> &goals,
                            std::vector<AstDiagnostic> &diagnostics) {
  for (const auto &goal : goals) {
    if (!goal)
      continue;
    if (const auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
      for (const auto &argument : call->call.args) {
        validateReasoningExpr(argument.value, goal.get(), diagnostics);
      }
    } else if (const auto assignment =
                   std::dynamic_pointer_cast<AssignGoal>(goal)) {
      validateReasoningExpr(assignment->expr, goal.get(), diagnostics);
    } else if (const auto multiAssignment =
                   std::dynamic_pointer_cast<MultiAssignGoal>(goal)) {
      validateReasoningExpr(multiAssignment->expr, goal.get(), diagnostics);
    } else if (const auto binary =
                   std::dynamic_pointer_cast<BinaryGoal>(goal)) {
      validateReasoningExpr(binary->left, goal.get(), diagnostics);
      validateReasoningExpr(binary->right, goal.get(), diagnostics);
    } else if (const auto returned =
                   std::dynamic_pointer_cast<ReturnGoal>(goal)) {
      for (const auto &field : returned->fields) {
        validateReasoningExpr(field.value, goal.get(), diagnostics);
      }
    } else if (const auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
      validateReasoningGoals(group->goals, diagnostics);
    } else if (const auto alternatives =
                   std::dynamic_pointer_cast<OrGoal>(goal)) {
      for (const auto &branch : alternatives->branches) {
        validateReasoningGoals(branch, diagnostics);
      }
    } else if (const auto conditional =
                   std::dynamic_pointer_cast<IfGoal>(goal)) {
      validateReasoningGoals({conditional->condition}, diagnostics);
      validateReasoningGoals(conditional->thenBranch, diagnostics);
      validateReasoningGoals(conditional->elseBranch, diagnostics);
    } else if (const auto where = std::dynamic_pointer_cast<WhereGoal>(goal)) {
      validateReasoningGoals({where->condition}, diagnostics);
    }
  }
}

} // namespace

struct AstAnalysisSession::Impl {
  static constexpr std::size_t MaxExactDuplicateFacts = 10000;

  struct DefinitionSummary {
    size_t count = 0;
    SourceSpan span;
    std::vector<SourceSpan> spans;
    // Head parameters of the last-seen declaration, mirroring how `span`
    // also tracks the last one.
    std::vector<SymbolParameter> params;
  };

  std::vector<AstDiagnostic> diagnostics;
  std::map<std::string, DefinitionSummary> methodDefinitions;
  std::map<std::string, DefinitionSummary> factDefinitions;
  std::set<std::string> globals;
  // All spans a global name was (re)bound at; the "unused global"
  // diagnostic below points at the last one, mirroring how
  // methodDefinitions/factDefinitions point at their last span too.
  std::map<std::string, std::vector<SourceSpan>> globalSpanLists;
  std::set<std::string> calls;
  std::unordered_set<std::string> factSignatures;
  std::size_t factsSeen = 0;
  bool duplicateCheckTruncated = false;
  SourceSpan duplicateCheckSpan;
};

AstAnalysisSession::AstAnalysisSession() : impl_(std::make_unique<Impl>()) {}
AstAnalysisSession::~AstAnalysisSession() = default;
AstAnalysisSession::AstAnalysisSession(AstAnalysisSession &&) noexcept =
    default;
AstAnalysisSession &
AstAnalysisSession::operator=(AstAnalysisSession &&) noexcept = default;

void AstAnalysisSession::consume(const std::shared_ptr<Statement> &stmt) {
  if (!stmt) {
    impl_->diagnostics.push_back(
        diagnosticFor(nullptr, "error", "felidae.parser.null_statement",
                      "Parser produced an invalid empty statement."));
    return;
  }
  if (auto binding = std::dynamic_pointer_cast<GlobalBindingStmt>(stmt)) {
    impl_->globals.insert(binding->name);
    impl_->globalSpanLists[binding->name].push_back(binding->sourceSpan);
    std::set<std::string> vars;
    collectExprUses(binding->expr, vars, impl_->calls);
    return;
  }

  auto clause = std::dynamic_pointer_cast<ClauseStmt>(stmt);
  if (!clause)
    return;

  if (clause->isFact()) {
    auto &definition = impl_->factDefinitions[clause->head.name];
    ++definition.count;
    definition.span = clause->sourceSpan;
    definition.spans.push_back(clause->sourceSpan);
    definition.params = collectHeadParameters(clause->head);
    ++impl_->factsSeen;
    if (impl_->factsSeen <= Impl::MaxExactDuplicateFacts) {
      const std::string signature = factSignature(*clause);
      if (!impl_->factSignatures.insert(signature).second) {
        warn(impl_->diagnostics, clause.get(), "felidae.fact.duplicate",
             "Duplicate fact declaration for '" + clause->head.name + "'.");
      }
    } else {
      impl_->duplicateCheckTruncated = true;
      impl_->duplicateCheckSpan = clause->sourceSpan;
    }
    return;
  }

  auto &definition = impl_->methodDefinitions[clause->head.name];
  ++definition.count;
  definition.span = clause->sourceSpan;
  definition.spans.push_back(clause->sourceSpan);
  definition.params = collectHeadParameters(clause->head);
  for (const auto &annotation : clause->annotations) {
    if (annotation.builtinId == BuiltinId::OverloadAnnotation ||
        annotation.builtinId == BuiltinId::MatcherAnnotation) {
      // Operator dispatch references the annotated implementation even
      // though no source-level TermExpr call names it.
      impl_->calls.insert(clause->head.name);
    } else {
      impl_->calls.insert(annotation.name);
    }
  }
  collectGlobalAssignmentCollisions(clause->body, impl_->globals,
                                    impl_->diagnostics);
  collectDiscardedExpressions(clause->body, clause->head.name,
                              impl_->diagnostics);
  validateReasoningGoals(clause->body, impl_->diagnostics);
  for (const auto &branch : clause->fallbackBranches) {
    collectGlobalAssignmentCollisions(branch, impl_->globals,
                                      impl_->diagnostics);
    collectDiscardedExpressions(branch, clause->head.name, impl_->diagnostics);
    validateReasoningGoals(branch, impl_->diagnostics);
  }
  std::set<std::string> declared;
  std::set<std::string> used;
  for (const auto &arg : clause->head.args) {
    if (!arg.name.empty())
      declared.insert(arg.name);
    if (auto var = std::dynamic_pointer_cast<VarExpr>(arg.value)) {
      if (!isIgnoredName(var->name) && !isLikelyTypeName(var->name)) {
        declared.insert(var->name);
      }
    }
  }
  collectAssignedNames(clause->body, declared);
  for (const auto &branch : clause->fallbackBranches) {
    collectAssignedNames(branch, declared);
  }
  for (const auto &goal : clause->body) {
    collectGoalUses(goal, used, impl_->calls);
  }
  for (const auto &branch : clause->fallbackBranches) {
    for (const auto &goal : branch) {
      collectGoalUses(goal, used, impl_->calls);
    }
  }
  for (const auto &name : declared) {
    if (!used.count(name)) {
      warn(impl_->diagnostics, clause.get(), "felidae.variable.unused",
           "Variable '" + name + "' is declared but never used in method '" +
               clause->head.name + "'.");
    }
  }
}

std::vector<AstDiagnostic> AstAnalysisSession::finish() {
  if (impl_->duplicateCheckTruncated) {
    impl_->diagnostics.push_back(diagnosticForSpan(
        impl_->duplicateCheckSpan, "warning",
        "felidae.analysis.duplicate_check_bounded",
        "Exact duplicate-fact diagnostics were bounded after " +
            std::to_string(Impl::MaxExactDuplicateFacts) +
            " facts to keep streaming analysis memory bounded."));
  }
  for (const auto &item : impl_->methodDefinitions) {
    if (item.first != "main" && !impl_->calls.count(item.first)) {
      impl_->diagnostics.push_back(diagnosticForSpan(
          item.second.span, "warning", "felidae.method.unused",
          "Method '" + item.first +
              "' is defined but not called in this file."));
    }
  }
  for (const auto &item : impl_->globals) {
    if (!impl_->calls.count(item)) {
      impl_->diagnostics.push_back(diagnosticForSpan(
          impl_->globalSpanLists[item].back(), "warning",
          "felidae.global.unused",
          "Global '" + item + "' is defined but not referenced in this file."));
    }
  }
  for (const auto &item : impl_->factDefinitions) {
    if (!impl_->calls.count(item.first)) {
      impl_->diagnostics.push_back(diagnosticForSpan(
          item.second.span, "warning", "felidae.fact.unused",
          "Fact type '" + item.first +
              "' is declared but not referenced in this file."));
    }
  }
  return std::move(impl_->diagnostics);
}

SymbolSummary AstAnalysisSession::symbols() const {
  auto toSymbolDefinitions = [](const auto &definitions) {
    std::vector<SymbolDefinition> result;
    result.reserve(definitions.size());
    for (const auto &[name, definition] : definitions) {
      result.push_back(SymbolDefinition{name, definition.count,
                                        definition.spans, definition.params});
    }
    return result;
  };

  SymbolSummary summary;
  summary.methods = toSymbolDefinitions(impl_->methodDefinitions);
  summary.facts = toSymbolDefinitions(impl_->factDefinitions);
  summary.globals.reserve(impl_->globals.size());
  for (const auto &name : impl_->globals) {
    // impl_->globals and impl_->globalSpanLists are always populated
    // together in consume(), so every global name has at least one span.
    // A global binding has no parameter list; the empty vector is stated
    // rather than left to aggregate initialization so adding a field to
    // SymbolDefinition does not silently leave one uninitialized here.
    summary.globals.push_back(
        SymbolDefinition{name, 1, impl_->globalSpanLists.at(name), {}});
  }
  return summary;
}

std::vector<AstDiagnostic> analyzeProgramAst(const Program &program) {
  AstAnalysisSession session;
  for (const auto &statement : program.statements)
    session.consume(statement);
  return session.finish();
}

} // namespace Felidae
