#include "IrCodeGenerator.h"

#include "IntegerParser.h"
#include "OperatorAnnotation.h"
#include "Symbol.h"
#include "form/SemanticOperation.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <unordered_set>

namespace Felidae {
namespace {

#ifndef NDEBUG
bool codegenTraceEnabled() {
  static const bool enabled = [] {
    const auto *value = std::getenv("FELIDAE_TRACE");
    return value != nullptr && *value != '\0' && std::string_view(value) != "0";
  }();
  return enabled;
}
#endif

struct DirectCompileContext {
  const std::unordered_set<SymbolId> &procedures;
  const std::unordered_set<SymbolId> &factTypes;
};

// Desugars ordered `where` guards into the existing IfGoal representation.
// Goals before a guard execute at their original level; goals after it run
// only when the condition succeeds. Reusing the same fallback branch at each
// level gives every failed guard the clause's established `else` behavior.
// This is a syntax-to-AST normalization only: it adds no opcode or execution
// path, and supports ordinary assignment sequences around guard statements.
std::vector<std::shared_ptr<Goal>> desugarWhereGuards(
    std::span<const std::shared_ptr<Goal>> remaining,
    const std::vector<std::shared_ptr<Goal>> &elseBranch) {
  if (remaining.empty())
    throw IntegerParserError("where-guarded clause requires a terminal goal");
  const auto guard = std::find_if(
      remaining.begin(), remaining.end(), [](const auto &goal) {
        return std::dynamic_pointer_cast<WhereGoal>(goal) != nullptr;
      });
  if (guard == remaining.end())
    return {remaining.begin(), remaining.end()};

  std::vector<std::shared_ptr<Goal>> result(remaining.begin(), guard);
  const auto where = std::dynamic_pointer_cast<WhereGoal>(*guard);
  const auto offset = static_cast<std::size_t>(guard - remaining.begin());
  auto thenBranch = desugarWhereGuards(remaining.subspan(offset + 1),
                                      elseBranch);
  auto ifGoal = std::make_shared<IfGoal>(
      where->condition, std::move(thenBranch), elseBranch);
  ifGoal->sourceSpan = where->sourceSpan;
  result.push_back(std::move(ifGoal));
  return result;
}

// Rewrites every where-guarded clause in the program to its desugared
// IfGoal-chain form, in place, before eligibility checks or lowering ever
// see it. `else`-without-`where` stays an explicit compile error (no
// competing implicit-guard grammar); a `where` guard with no `else` compiles
// to an implicit else branch that throws BuiltinId::WhereGuardFailed at
// runtime -- a clear failure instead of a silent nil, and instead of
// rejecting valid guard-only clauses at compile time.
void desugarWhereGuardedClauses(Program &program) {
  for (auto &clause : program.clauses) {
    if (!clause || clause->isFact())
      continue;
    const bool hasWhereGuard = std::any_of(
        clause->body.begin(), clause->body.end(), [](const auto &goal) {
          return std::dynamic_pointer_cast<WhereGoal>(goal) != nullptr;
        });
    if (!hasWhereGuard && clause->fallbackBranches.empty())
      continue;
    if (!hasWhereGuard) {
      throw IntegerParserError(
          "not yet lowered to IR: else without a where guard at " +
          std::to_string(clause->sourceSpan.startLine) + ":" +
          std::to_string(clause->sourceSpan.startColumn));
    }
    std::vector<std::shared_ptr<Goal>> elseBranch;
    if (clause->fallbackBranches.empty()) {
      auto failCall = std::make_shared<TermExpr>(
          "where:guardFailed", std::vector<Arg>{}, BuiltinId::WhereGuardFailed);
      failCall->sourceSpan = clause->sourceSpan;
      auto returned =
          std::make_shared<ReturnGoal>(std::vector<Arg>{Arg({}, failCall)});
      returned->sourceSpan = clause->sourceSpan;
      elseBranch.push_back(std::move(returned));
    } else if (clause->fallbackBranches.size() == 1) {
      elseBranch = clause->fallbackBranches.front();
    } else {
      throw IntegerParserError(
          "not yet lowered to IR: a where-guarded clause requires at most "
          "one else branch at " +
          std::to_string(clause->sourceSpan.startLine) + ":" +
          std::to_string(clause->sourceSpan.startColumn));
    }
    auto rewritten = std::make_shared<ClauseStmt>(*clause);
    rewritten->body = desugarWhereGuards(clause->body, elseBranch);
    rewritten->fallbackBranches.clear();
    clause = std::move(rewritten);
  }
}

// Defined later in this file, alongside resolveScopedAccess whose traversal
// shape it mirrors; forward-declared here for FactQueryNormalizer::iteration().
std::shared_ptr<Expr> substituteCaptures(
    const std::shared_ptr<Expr> &expression, const std::string &rowParameter,
    SymbolId rowParameterId, const std::string &capturesVariable,
    SymbolId capturesVariableId,
    std::vector<std::pair<std::string, SymbolId>> &captured,
    std::unordered_set<SymbolId> &capturedIds);

// Fact queries are source conveniences over the existing ForEachFact IR
// operation. Each predicate becomes an ordinary compiler-generated procedure;
// RegisterVm therefore retains one iteration/call path for both lambda(...)
// and concrete type methods such as School.all() and School.select(...).
class FactQueryNormalizer {
public:
  explicit FactQueryNormalizer(const Program &program) {
    for (const auto &clause : program.clauses) {
      if (!clause)
        continue;
      occupied_.insert(clause->head.nameId);
      if (clause->isFact())
        factTypes_.emplace(clause->head.name, clause->head.nameId);
    }
  }

  void normalize(Program &program) {
    for (auto &binding : program.globals)
      if (binding)
        binding->expr = expression(binding->expr);
    for (auto &clause : program.clauses) {
      if (!clause || clause->isFact())
        continue;
      goals(clause->body);
      for (auto &branch : clause->fallbackBranches)
        goals(branch);
    }
    program.clauses.insert(program.clauses.end(), generated_.begin(),
                           generated_.end());
  }

private:
  std::optional<std::pair<std::string, SymbolId>>
  queryRoot(const std::shared_ptr<Expr> &value) const {
    const auto term = std::dynamic_pointer_cast<TermExpr>(value);
    if (!term)
      return std::nullopt;
    if (term->name.rfind("__query:", 0) == 0 && !term->args.empty())
      return queryRoot(term->args.front().value);
    const auto separator = term->name.rfind('.');
    if (separator == std::string::npos)
      return std::nullopt;
    const auto typeName = term->name.substr(0, separator);
    const auto type = factTypes_.find(typeName);
    if (type == factTypes_.end())
      return std::nullopt;
    return std::pair{typeName, type->second};
  }

  bool conditionChain(const std::shared_ptr<Expr> &value) const {
    const auto term = std::dynamic_pointer_cast<TermExpr>(value);
    if (!term)
      return false;
    if ((term->name == "__query:AndWhere" ||
         term->name == "__query:OrWhere") &&
        !term->args.empty())
      return conditionChain(term->args.front().value);
    const auto separator = term->name.rfind('.');
    return separator != std::string::npos &&
           term->name.substr(separator + 1) == "where";
  }

  std::shared_ptr<Expr> iteration(std::string typeName, SymbolId typeId,
                                  std::string variable, SymbolId variableId,
                                  std::shared_ptr<Expr> body,
                                  const SourceSpan &sourceSpan) {
    std::string procedureName;
    SymbolId procedureId = 0;
    do {
      procedureName = "__fact_query_" + std::to_string(nextProcedure_++);
      procedureId = symbolIdForName(procedureName);
    } while (occupied_.contains(procedureId));
    occupied_.insert(procedureId);

    // A predicate that reads an outer-scope value (`r.from == tiger_female`,
    // not just a literal) closes over it: the synthesized callback below has
    // no access to the caller's own registers, so every such reference is
    // rewritten to read from a second, explicit __captures parameter
    // instead, and the caller builds that map from its own scope where the
    // values are actually defined. See substituteCaptures() and
    // IrOpcode::ForEachFact's fifth operand in RegisterVm.cpp.
    const std::string capturesVariable = "__captures";
    const SymbolId capturesVariableId = symbolIdForName(capturesVariable);
    std::vector<std::pair<std::string, SymbolId>> captured;
    std::unordered_set<SymbolId> capturedIds;
    body = substituteCaptures(body, variable, variableId, capturesVariable,
                              capturesVariableId, captured, capturedIds);

    Call head(procedureName, procedureId,
              {Arg(variable, variableId,
                   std::make_shared<VarExpr>(typeName, typeId)),
               Arg(capturesVariable, capturesVariableId,
                   std::make_shared<VarExpr>("any", symbolIdForName("any")))});
    auto returned = std::make_shared<ReturnGoal>(
        std::vector<Arg>{Arg(std::string{}, std::move(body))});
    returned->sourceSpan = sourceSpan;
    auto procedure = std::make_shared<ClauseStmt>(
        std::move(head), std::string{},
        std::vector<std::shared_ptr<Goal>>{std::move(returned)},
        std::vector<std::vector<std::shared_ptr<Goal>>>{}, false,
        ClauseKind::Method);
    procedure->sourceSpan = sourceSpan;
    generated_.push_back(std::move(procedure));

    std::vector<MapEntry> captureEntries;
    captureEntries.reserve(captured.size());
    for (const auto &[name, id] : captured) {
      auto reference = std::make_shared<VarExpr>(name, id);
      reference->sourceSpan = sourceSpan;
      captureEntries.emplace_back(name, id, std::move(reference));
    }
    auto capturesMap = std::make_shared<MapExpr>(std::move(captureEntries));
    capturesMap->sourceSpan = sourceSpan;

    auto result = std::make_shared<TermExpr>(
        "for_each_fact",
        std::vector<Arg>{
            Arg(std::string{},
                std::make_shared<VarExpr>(typeName, typeId)),
            Arg(std::string{},
                std::make_shared<VarExpr>(procedureName, procedureId)),
            Arg(std::string{}, std::move(capturesMap))});
    result->sourceSpan = sourceSpan;
    return result;
  }

  std::shared_ptr<Expr> expression(std::shared_ptr<Expr> value) {
    if (!value)
      return value;
    if (const auto lambda = std::dynamic_pointer_cast<LambdaExpr>(value)) {
      lambda->source = expression(lambda->source);
      lambda->body = expression(lambda->body);
      if (lambda->right)
        lambda->right = expression(lambda->right);
      const auto source = std::dynamic_pointer_cast<VarExpr>(lambda->source);
      const auto type = source ? factTypes_.find(source->name)
                               : factTypes_.end();
      if (!source || type == factTypes_.end() || type->second != source->nameId)
        return value;
      return iteration(source->name, source->nameId, lambda->variable,
                       lambda->variableId, lambda->body, lambda->sourceSpan);
    }
    if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) {
      for (auto &item : array->items)
        item = expression(item);
    } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(value)) {
      for (auto &entry : map->entries)
        entry.value = expression(entry.value);
    } else if (const auto access =
                   std::dynamic_pointer_cast<AccessExpr>(value)) {
      access->target = expression(access->target);
    } else if (const auto term =
                   std::dynamic_pointer_cast<TermExpr>(value)) {
      if (term->name == "__query:limit") {
        if (term->args.size() != 2 || term->args[1].name != "records")
          throw IntegerParserError(
              "limit requires exactly one named records argument");
        auto result = std::make_shared<TermExpr>(
            "array:limit",
            std::vector<Arg>{Arg(std::string{}, expression(term->args[0].value)),
                             Arg(std::string{}, expression(term->args[1].value))},
            BuiltinId::ArrayLimit);
        result->sourceSpan = term->sourceSpan;
        return result;
      }
      if (term->name == "__query:AndWhere" ||
          term->name == "__query:OrWhere") {
        if (term->args.size() < 2)
          throw IntegerParserError(term->name.substr(8) +
                                   " requires a condition");
        if (!conditionChain(term->args[0].value))
          throw IntegerParserError(term->name.substr(8) +
                                   " must follow where/AndWhere/OrWhere");
        const auto root = queryRoot(term->args[0].value);
        if (!root)
          throw IntegerParserError(term->name.substr(8) +
                                   " requires a fact query target");
        std::vector<Arg> condition(term->args.begin() + 1, term->args.end());
        auto next = std::make_shared<TermExpr>(
            root->first + ".where", std::move(condition));
        next->sourceSpan = term->sourceSpan;
        auto result = std::make_shared<TermExpr>(
            "fact:setCombine",
            std::vector<Arg>{
                Arg(std::string{}, expression(term->args[0].value)),
                Arg(std::string{}, expression(std::move(next))),
                Arg(std::string{}, std::make_shared<NumberExpr>(
                                       term->name == "__query:AndWhere" ? 0.0
                                                                         : 1.0))},
            BuiltinId::FactSetCombine);
        result->sourceSpan = term->sourceSpan;
        return result;
      }
      for (auto &argument : term->args)
        argument.value = expression(argument.value);
      if (term->name == "ancestorAnalysis") {
        // A source convenience over the three already-real hierarchy
        // intrinsics (commonAncestors/lowestCommonAncestor/
        // highestCommonAncestor, each its own dedicated IrOpcode -- see
        // IrOpcode::HierarchyCommonAncestors and friends), combined into one
        // fact instead of a bespoke analysis opcode. Facts carry more
        // reasoning context than a bare map, matching how every other
        // multi-value result in this file returns a labeled record.
        if (term->args.size() != 2)
          throw IntegerParserError(
              "ancestorAnalysis requires left and right arguments");
        std::shared_ptr<Expr> leftArg;
        std::shared_ptr<Expr> rightArg;
        for (const auto &argument : term->args) {
          if (argument.name == "left")
            leftArg = argument.value;
          else if (argument.name == "right")
            rightArg = argument.value;
        }
        if (!leftArg || !rightArg) {
          throw IntegerParserError(
              "ancestorAnalysis requires named left and right arguments");
        }
        const auto hierarchyCall = [&](std::string name) {
          std::vector<Arg> callArgs;
          callArgs.emplace_back("left", symbolIdForName("left"), leftArg);
          callArgs.emplace_back("right", symbolIdForName("right"), rightArg);
          auto call = std::make_shared<TermExpr>(std::move(name),
                                                  std::move(callArgs));
          call->sourceSpan = term->sourceSpan;
          return call;
        };
        std::vector<Arg> factArgs;
        factArgs.emplace_back("common", symbolIdForName("common"),
                              hierarchyCall("commonAncestors"));
        factArgs.emplace_back("lowest", symbolIdForName("lowest"),
                              hierarchyCall("lowestCommonAncestor"));
        factArgs.emplace_back("highest", symbolIdForName("highest"),
                              hierarchyCall("highestCommonAncestor"));
        auto result = std::make_shared<TermExpr>(
            "AncestorAnalysis", symbolIdForName("AncestorAnalysis"),
            std::move(factArgs), BuiltinId::Unknown, /*capitalized=*/true);
        result->sourceSpan = term->sourceSpan;
        return result;
      }
      const auto separator = term->name.rfind('.');
      if (separator == std::string::npos)
        return value;
      const auto typeName = term->name.substr(0, separator);
      const auto method = term->name.substr(separator + 1);
      const auto type = factTypes_.find(typeName);
      if (type == factTypes_.end())
        return value;
      const auto rowName = "__fact";
      const auto rowId = symbolIdForName(rowName);
      const auto mapArguments = [&](const std::shared_ptr<Expr> &value,
                                    std::string_view label) {
        const auto map = std::dynamic_pointer_cast<MapExpr>(value);
        if (!map)
          throw IntegerParserError(typeName + "." + method + " " +
                                   std::string(label) + " must be a map");
        std::vector<Arg> result;
        result.reserve(map->entries.size());
        for (const auto &entry : map->entries)
          result.emplace_back(entry.key, entry.keyId, entry.value);
        return result;
      };
      const auto queryFromMatch = [&](const std::shared_ptr<Expr> &match) {
        auto query = std::make_shared<TermExpr>(
            typeName + ".where", mapArguments(match, "match"));
        query->sourceSpan = term->sourceSpan;
        return expression(std::move(query));
      };
      const auto named = [&](std::string_view name) -> std::shared_ptr<Expr> {
        const auto found = std::find_if(term->args.begin(), term->args.end(),
                                        [&](const Arg &argument) {
                                          return argument.name == name;
                                        });
        return found == term->args.end() ? nullptr : found->value;
      };
      if (method == "insert") {
        const auto values = named("values");
        const auto source = named("source");
        if (!values || term->args.size() != (source ? 2u : 1u))
          throw IntegerParserError(
              typeName + ".insert requires values and optional source");
        auto result = std::make_shared<TermExpr>(
            "fact:insert",
            std::vector<Arg>{
                Arg(std::string{}, std::make_shared<StringExpr>(
                                       typeName, symbolPiecesForId(type->second))),
                Arg(std::string{}, values),
                Arg(std::string{}, source ? source
                                          : std::make_shared<NilExpr>())},
            BuiltinId::FactInsert);
        result->sourceSpan = term->sourceSpan;
        return result;
      }
      if (method == "update") {
        const auto match = named("match");
        const auto values = named("values");
        if (!match || !values || term->args.size() != 2)
          throw IntegerParserError(typeName +
                                   ".update requires match and values maps");
        auto result = std::make_shared<TermExpr>(
            "fact:update",
            std::vector<Arg>{Arg(std::string{}, queryFromMatch(match)),
                             Arg(std::string{}, values)},
            BuiltinId::FactUpdate);
        result->sourceSpan = term->sourceSpan;
        return result;
      }
      if (method == "delete") {
        const auto match = named("match");
        if (!match || term->args.size() != 1)
          throw IntegerParserError(typeName + ".delete requires a match map");
        auto result = std::make_shared<TermExpr>(
            "fact:delete",
            std::vector<Arg>{Arg(std::string{}, queryFromMatch(match))},
            BuiltinId::FactDelete);
        result->sourceSpan = term->sourceSpan;
        return result;
      }
      if (method == "join" || method == "leftJoin" ||
          method == "rightJoin" || method == "OuterJoin") {
        const auto other = std::dynamic_pointer_cast<VarExpr>(named("type"));
        const auto left = std::dynamic_pointer_cast<StringExpr>(named("left"));
        const auto right = std::dynamic_pointer_cast<StringExpr>(named("right"));
        if (!other || !factTypes_.contains(other->name) || !left || !right ||
            term->args.size() != 3)
          throw IntegerParserError(typeName +
                                   "." + method +
                                   " requires type, left, and right");
        const auto kind = std::make_shared<NumberExpr>(
            method == "join"        ? 0.0
            : method == "leftJoin"  ? 1.0
            : method == "rightJoin" ? 2.0
                                      : 3.0);
        auto leftRows = std::make_shared<TermExpr>(typeName + ".all",
                                                   std::vector<Arg>{});
        auto rightRows = std::make_shared<TermExpr>(other->name + ".all",
                                                    std::vector<Arg>{});
        auto result = std::make_shared<TermExpr>(
            "fact:join",
            std::vector<Arg>{Arg(std::string{}, expression(leftRows)),
                             Arg(std::string{}, expression(rightRows)),
                             Arg(std::string{}, left), Arg(std::string{}, right),
                             Arg(std::string{}, kind)},
            BuiltinId::FactJoin);
        result->sourceSpan = term->sourceSpan;
        return result;
      }
      const auto matchingRows = [&]() {
        const auto match = named("match");
        if (match)
          return queryFromMatch(match);
        return iteration(typeName, type->second, rowName, rowId,
                         std::make_shared<VarExpr>(rowName, rowId),
                         term->sourceSpan);
      };
      if (method == "count") {
        if (term->args.size() > 1 ||
            (term->args.size() == 1 && !named("match")))
          throw IntegerParserError(
              typeName + ".count accepts only an optional match map");
        auto rows = matchingRows();
        auto result = std::make_shared<TermExpr>(
            "array:len",
            std::vector<Arg>{Arg(std::string{}, std::move(rows))},
            BuiltinId::ArrayLen);
        result->sourceSpan = term->sourceSpan;
        return result;
      }
      if (method == "sum" || method == "average" || method == "min" ||
          method == "max") {
        const auto field = std::dynamic_pointer_cast<StringExpr>(named("field"));
        if (!field || term->args.size() > 2 ||
            (term->args.size() == 2 && !named("match")))
          throw IntegerParserError(typeName + "." + method +
                                   " requires field and optional match");
        const double operation = method == "sum"       ? 0.0
                                 : method == "average" ? 1.0
                                 : method == "min"     ? 2.0
                                                        : 3.0;
        auto result = std::make_shared<TermExpr>(
            "fact:aggregate",
            std::vector<Arg>{Arg(std::string{}, matchingRows()),
                             Arg(std::string{}, field),
                             Arg(std::string{},
                                 std::make_shared<NumberExpr>(operation))},
            BuiltinId::FactAggregate);
        result->sourceSpan = term->sourceSpan;
        return result;
      }
      if (method == "all") {
        if (!term->args.empty())
          throw IntegerParserError(typeName + ".all requires no arguments");
        return iteration(typeName, type->second, rowName, rowId,
                         std::make_shared<VarExpr>(rowName, rowId),
                         term->sourceSpan);
      }
      if (method == "select") {
        const auto fields = std::dynamic_pointer_cast<ArrayExpr>(named("fields"));
        if (!fields || term->args.size() > 2 ||
            (term->args.size() == 2 && !named("match")))
          throw IntegerParserError(typeName +
                                   ".select requires fields and optional match");
        if (std::any_of(fields->items.begin(), fields->items.end(),
                        [](const auto &field) {
                          return !std::dynamic_pointer_cast<StringExpr>(field);
                        }))
          throw IntegerParserError(typeName +
                                   ".select fields must contain text names");
        auto result = std::make_shared<TermExpr>(
            "fact:project",
            std::vector<Arg>{Arg(std::string{}, matchingRows()),
                             Arg(std::string{}, fields)},
            BuiltinId::FactProject);
        result->sourceSpan = term->sourceSpan;
        return result;
      }
      if (method == "where") {
        if (term->args.size() == 1 &&
            term->args.front().name == "using") {
          const auto callback =
              std::dynamic_pointer_cast<VarExpr>(term->args.front().value);
          if (!callback)
            throw IntegerParserError(typeName +
                                     ".where using must name a procedure");
          auto result = std::make_shared<TermExpr>(
              "for_each_fact",
              std::vector<Arg>{
                  Arg(std::string{},
                      std::make_shared<VarExpr>(typeName, type->second)),
                  Arg(std::string{}, callback)},
              BuiltinId::Unknown);
          result->sourceSpan = term->sourceSpan;
          return result;
        }
        if (term->args.empty() ||
            std::any_of(term->args.begin(), term->args.end(),
                        [](const Arg &argument) {
                          return argument.name.empty();
                        })) {
          throw IntegerParserError(typeName + "." + method +
                                   " requires named field values");
        }
        std::shared_ptr<Expr> predicate;
        for (const auto &argument : term->args) {
          auto field = std::make_shared<AccessExpr>(
              std::make_shared<VarExpr>(rowName, rowId), argument.name);
          field->keyId = argument.nameId;
          auto comparison = std::make_shared<OperatorExpression>(
              CoreOperator::StrictEqual, std::move(field), argument.value);
          predicate = predicate
                          ? std::make_shared<OperatorExpression>(
                                CoreOperator::LogicalAnd, std::move(predicate),
                                std::move(comparison))
                          : std::move(comparison);
        }
        return iteration(typeName, type->second, rowName, rowId,
                         std::move(predicate), term->sourceSpan);
      }
    } else if (const auto operation =
                   std::dynamic_pointer_cast<OperatorExpression>(value)) {
      std::vector<OperatorCapture> captures;
      captures.reserve(operation->captureCount());
      for (std::size_t index = 0; index < operation->captureCount(); ++index) {
        captures.emplace_back(std::string(operation->captureName(index)),
                              expression(operation->capture(index)));
      }
      std::shared_ptr<OperatorExpression> rewritten;
      if (operation->coreOperator == CoreOperator::Unknown) {
        rewritten = std::make_shared<OperatorExpression>(
            operation->operatorId, operation->patternId, std::move(captures),
            operation->explicitlyGrouped, operation->resolvedMethodId);
      } else if (captures.size() == 1) {
        rewritten = std::make_shared<OperatorExpression>(
            operation->coreOperator, captures.front().expression);
      } else {
        rewritten = std::make_shared<OperatorExpression>(
            operation->coreOperator, captures.at(0).expression,
            captures.at(1).expression);
      }
      rewritten->module = operation->module;
      rewritten->sourceSpan = operation->sourceSpan;
      return rewritten;
    }
    return value;
  }

  void goals(std::vector<std::shared_ptr<Goal>> &items) {
    for (auto &goal : items) {
      if (const auto assignment = std::dynamic_pointer_cast<AssignGoal>(goal))
        assignment->expr = expression(assignment->expr);
      else if (const auto assignment =
                   std::dynamic_pointer_cast<MultiAssignGoal>(goal))
        assignment->expr = expression(assignment->expr);
      else if (const auto returned =
                   std::dynamic_pointer_cast<ReturnGoal>(goal))
        for (auto &field : returned->fields)
          field.value = expression(field.value);
      else if (const auto binary =
                   std::dynamic_pointer_cast<BinaryGoal>(goal)) {
        binary->left = expression(binary->left);
        binary->right = expression(binary->right);
      } else if (const auto call =
                     std::dynamic_pointer_cast<CallGoal>(goal)) {
        for (auto &argument : call->call.args)
          argument.value = expression(argument.value);
      } else if (const auto conditional =
                     std::dynamic_pointer_cast<IfGoal>(goal)) {
        std::vector<std::shared_ptr<Goal>> condition{conditional->condition};
        goals(condition);
        conditional->condition = condition.front();
        goals(conditional->thenBranch);
        goals(conditional->elseBranch);
      }
    }
  }

  std::unordered_map<std::string, SymbolId> factTypes_;
  std::unordered_set<SymbolId> occupied_;
  std::vector<std::shared_ptr<ClauseStmt>> generated_;
  std::size_t nextProcedure_ = 0;
};

void normalizeFactQueries(Program &program) {
  FactQueryNormalizer(program).normalize(program);
}

// The integer parser preserves a dotted identifier as one symbol because the
// same SentencePiece sequence is also used for qualified calls and `fx.`
// keys.  The compiler is the first phase with lexical scope information, so
// it can safely reinterpret only `local.field` spellings whose first segment
// is a visible binding.  Unscoped qualified symbols stay untouched.
std::shared_ptr<Expr>
resolveScopedAccess(const std::shared_ptr<Expr> &expression,
                    const std::unordered_set<SymbolId> &visible) {
  if (!expression)
    return expression;
  if (const auto variable = std::dynamic_pointer_cast<VarExpr>(expression)) {
    const auto dot = variable->name.find('.');
    if (dot == std::string::npos)
      return expression;
    const auto baseName = variable->name.substr(0, dot);
    if (!visible.contains(symbolIdForName(baseName)))
      return expression;
    std::shared_ptr<Expr> result =
        std::make_shared<VarExpr>(baseName, symbolIdForName(baseName));
    result->sourceSpan = variable->sourceSpan;
    std::size_t begin = dot + 1;
    while (begin < variable->name.size()) {
      const auto next = variable->name.find('.', begin);
      const auto key = variable->name.substr(
          begin, next == std::string::npos ? std::string::npos : next - begin);
      if (key.empty())
        return expression;
      auto access = std::make_shared<AccessExpr>(std::move(result), key);
      access->sourceSpan = variable->sourceSpan;
      result = std::move(access);
      if (next == std::string::npos)
        break;
      begin = next + 1;
    }
    return result;
  }
  if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(expression)) {
    for (auto &item : array->items)
      item = resolveScopedAccess(item, visible);
  } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(expression)) {
    for (auto &item : map->entries)
      item.value = resolveScopedAccess(item.value, visible);
  } else if (const auto access =
                 std::dynamic_pointer_cast<AccessExpr>(expression)) {
    access->target = resolveScopedAccess(access->target, visible);
  } else if (const auto term =
                 std::dynamic_pointer_cast<TermExpr>(expression)) {
    for (auto &argument : term->args)
      argument.value = resolveScopedAccess(argument.value, visible);
  } else if (const auto operation =
                 std::dynamic_pointer_cast<OperatorExpression>(expression)) {
    std::shared_ptr<OperatorExpression> rewritten;
    if (operation->coreOperator == CoreOperator::Unknown) {
      std::vector<OperatorCapture> captures;
      captures.reserve(operation->captureCount());
      for (std::size_t index = 0; index < operation->captureCount(); ++index) {
        captures.emplace_back(
            std::string(operation->captureName(index)),
            resolveScopedAccess(operation->capture(index), visible));
      }
      rewritten = std::make_shared<OperatorExpression>(
          operation->operatorId, operation->patternId, std::move(captures),
          operation->explicitlyGrouped, operation->resolvedMethodId);
    } else if (operation->captureCount() == 1) {
      rewritten = std::make_shared<OperatorExpression>(
          operation->coreOperator,
          resolveScopedAccess(operation->capture(0), visible));
    } else {
      rewritten = std::make_shared<OperatorExpression>(
          operation->coreOperator,
          resolveScopedAccess(operation->capture(0), visible),
          resolveScopedAccess(operation->capture(1), visible));
    }
    rewritten->module = operation->module;
    rewritten->sourceSpan = operation->sourceSpan;
    return rewritten;
  }
  return expression;
}

// Rewrites every free-variable reference in a fact-query predicate body (any
// VarExpr other than the row parameter itself) into a read from the
// synthesized callback's captures map, and records each one captured
// (deduplicated, first-seen order) into `captured`. Used by
// FactQueryNormalizer::iteration() -- see the comment there for why this
// exists. Mirrors resolveScopedAccess's traversal shape immediately above.
std::shared_ptr<Expr> substituteCaptures(
    const std::shared_ptr<Expr> &expression, const std::string &rowParameter,
    SymbolId rowParameterId, const std::string &capturesVariable,
    SymbolId capturesVariableId,
    std::vector<std::pair<std::string, SymbolId>> &captured,
    std::unordered_set<SymbolId> &capturedIds) {
  if (!expression)
    return expression;
  if (const auto variable = std::dynamic_pointer_cast<VarExpr>(expression)) {
    if (variable->nameId == rowParameterId || variable->nameId == 0)
      return expression;
    // This compiler pass runs before resolveScopedAccess ever splits a
    // dotted name into an AccessExpr chain (see that function above), so the
    // row parameter's own field access (`s.students`) still arrives here as
    // one VarExpr spelled "s.students", not yet AccessExpr(s, "students").
    // Recognize that shape by name prefix and leave it untouched -- only
    // resolveScopedAccess is allowed to decide it means field access, this
    // pass must not misread it as a free variable named "s.students".
    if (variable->name.size() > rowParameter.size() &&
        variable->name.compare(0, rowParameter.size(), rowParameter) == 0 &&
        variable->name[rowParameter.size()] == '.') {
      return expression;
    }
    if (capturedIds.insert(variable->nameId).second)
      captured.emplace_back(variable->name, variable->nameId);
    auto access = std::make_shared<AccessExpr>(
        std::make_shared<VarExpr>(capturesVariable, capturesVariableId),
        variable->name);
    access->keyId = variable->nameId;
    access->sourceSpan = variable->sourceSpan;
    return access;
  }
  const auto recurse = [&](const std::shared_ptr<Expr> &child) {
    return substituteCaptures(child, rowParameter, rowParameterId,
                              capturesVariable, capturesVariableId, captured,
                              capturedIds);
  };
  if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(expression)) {
    for (auto &item : array->items)
      item = recurse(item);
  } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(expression)) {
    for (auto &item : map->entries)
      item.value = recurse(item.value);
  } else if (const auto access =
                 std::dynamic_pointer_cast<AccessExpr>(expression)) {
    // The row parameter's own fields (r.from) stay untouched: substitution
    // only ever replaces the target, and the row VarExpr itself is excluded
    // above, so `r.from` recurses into `r` (unchanged) then stops.
    access->target = recurse(access->target);
  } else if (const auto term =
                 std::dynamic_pointer_cast<TermExpr>(expression)) {
    for (auto &argument : term->args)
      argument.value = recurse(argument.value);
  } else if (const auto operation =
                 std::dynamic_pointer_cast<OperatorExpression>(expression)) {
    std::shared_ptr<OperatorExpression> rewritten;
    if (operation->coreOperator == CoreOperator::Unknown) {
      std::vector<OperatorCapture> captures;
      captures.reserve(operation->captureCount());
      for (std::size_t index = 0; index < operation->captureCount(); ++index) {
        captures.emplace_back(std::string(operation->captureName(index)),
                              recurse(operation->capture(index)));
      }
      rewritten = std::make_shared<OperatorExpression>(
          operation->operatorId, operation->patternId, std::move(captures),
          operation->explicitlyGrouped, operation->resolvedMethodId);
    } else if (operation->captureCount() == 1) {
      rewritten = std::make_shared<OperatorExpression>(
          operation->coreOperator, recurse(operation->capture(0)));
    } else {
      rewritten = std::make_shared<OperatorExpression>(
          operation->coreOperator, recurse(operation->capture(0)),
          recurse(operation->capture(1)));
    }
    rewritten->module = operation->module;
    rewritten->sourceSpan = operation->sourceSpan;
    return rewritten;
  }
  return expression;
}

void resolveScopedAccessesInGoals(std::vector<std::shared_ptr<Goal>> &goals,
                                  std::unordered_set<SymbolId> visible) {
  for (auto &goal : goals) {
    if (const auto assignment = std::dynamic_pointer_cast<AssignGoal>(goal)) {
      assignment->expr = resolveScopedAccess(assignment->expr, visible);
      visible.insert(assignment->nameId);
    } else if (const auto returned =
                   std::dynamic_pointer_cast<ReturnGoal>(goal)) {
      for (auto &field : returned->fields)
        field.value = resolveScopedAccess(field.value, visible);
    } else if (const auto binary =
                   std::dynamic_pointer_cast<BinaryGoal>(goal)) {
      binary->left = resolveScopedAccess(binary->left, visible);
      binary->right = resolveScopedAccess(binary->right, visible);
    } else if (const auto conditional =
                   std::dynamic_pointer_cast<IfGoal>(goal)) {
      std::vector<std::shared_ptr<Goal>> condition{conditional->condition};
      resolveScopedAccessesInGoals(condition, visible);
      conditional->condition = condition.front();
      resolveScopedAccessesInGoals(conditional->thenBranch, visible);
      resolveScopedAccessesInGoals(conditional->elseBranch, visible);
    }
  }
}

// Annotated mixfix/overload methods may declare their capture bindings in the
// operator annotation rather than repeat them in the callable head.  They are
// still ordinary procedure parameters in IR: this helper gives lowering one
// canonical parameter list for both forms.
std::vector<SymbolId> procedureParameters(const ClauseStmt &method) {
  std::vector<SymbolId> parameters;
  parameters.reserve(method.head.args.size());
  for (const auto &parameter : method.head.args) {
    if (parameter.name.empty() || parameter.nameId == 0) {
      throw IntegerParserError(
          "not yet lowered to IR: invalid procedure parameter");
    }
    if (std::find(parameters.begin(), parameters.end(), parameter.nameId) !=
        parameters.end()) {
      throw IntegerParserError(
          "not yet lowered to IR: duplicate procedure parameter");
    }
    parameters.push_back(parameter.nameId);
  }
  if (!parameters.empty())
    return parameters;

  for (const auto &annotation : method.annotations) {
    if (annotation.builtinId != BuiltinId::MixfixAnnotation &&
        annotation.builtinId != BuiltinId::OverloadAnnotation)
      continue;
    const auto parsed = decodeOperatorAnnotation(annotation);
    if (parsed.captures.empty())
      continue;
    for (const auto &capture : parsed.captures) {
      if (capture.nameId == 0 ||
          std::find(parameters.begin(), parameters.end(), capture.nameId) !=
              parameters.end()) {
        throw IntegerParserError(
            "not yet lowered to IR: invalid annotated capture parameter");
      }
      parameters.push_back(capture.nameId);
    }
    return parameters;
  }
  return parameters;
}

// The strict module compiler is intentionally closed: every emitted
// LoadSymbol/Call must resolve through typed module globals or procedures.
// No instruction can reconstruct an AST value or invoke Interpreter services.
bool isDirectDeterministicExpression(
    const std::shared_ptr<Expr> &expression,
    const std::unordered_set<SymbolId> &definedSymbols,
    const DirectCompileContext &context) {
  if (!expression)
    return false;
  if (std::dynamic_pointer_cast<NumberExpr>(expression) ||
      std::dynamic_pointer_cast<BoolExpr>(expression) ||
      std::dynamic_pointer_cast<StringExpr>(expression) ||
      std::dynamic_pointer_cast<NilExpr>(expression)) {
    return true;
  }
  if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(expression)) {
    return std::all_of(
        array->items.begin(), array->items.end(), [&](const auto &item) {
          return isDirectDeterministicExpression(item, definedSymbols, context);
        });
  }
  if (const auto map = std::dynamic_pointer_cast<MapExpr>(expression)) {
    return std::all_of(map->entries.begin(), map->entries.end(),
                       [&](const MapEntry &entry) {
                         return isDirectDeterministicExpression(
                             entry.value, definedSymbols, context);
                       });
  }
  if (const auto access = std::dynamic_pointer_cast<AccessExpr>(expression)) {
    return isDirectDeterministicExpression(access->target, definedSymbols,
                                           context);
  }
  if (const auto operation =
          std::dynamic_pointer_cast<OperatorExpression>(expression)) {
    if (operation->coreOperator == CoreOperator::Then) {
      if (operation->captureCount() != 2 ||
          !isDirectDeterministicExpression(operation->capture(0),
                                           definedSymbols, context)) {
        return false;
      }
      auto pipelineSymbols = definedSymbols;
      pipelineSymbols.insert(symbolIdForName("system.result"));
      return isDirectDeterministicExpression(operation->capture(1),
                                             pipelineSymbols, context);
    }
    if (operation->coreOperator == CoreOperator::Unknown &&
        (operation->resolvedMethodId == 0 ||
         !context.procedures.contains(operation->resolvedMethodId)))
      return false;
    for (std::size_t index = 0; index < operation->captureCount(); ++index) {
      if (!isDirectDeterministicExpression(operation->capture(index),
                                           definedSymbols, context))
        return false;
    }
    return true;
  }
  if (const auto variable = std::dynamic_pointer_cast<VarExpr>(expression)) {
    return definedSymbols.contains(variable->nameId);
  }
  if (const auto term = std::dynamic_pointer_cast<TermExpr>(expression)) {
    if (builtinOperationArity(term->builtinId)) {
      std::vector<std::string_view> names;
      names.reserve(term->args.size());
      for (const auto &argument : term->args)
        names.push_back(argument.name);
      return builtinOperationArgumentOrder(term->builtinId, names) &&
             std::all_of(term->args.begin(), term->args.end(),
                         [&](const Arg &argument) {
                           return isDirectDeterministicExpression(
                               argument.value, definedSymbols, context);
                         });
    }
    if (const auto operation = tensorOperationForName(term->name)) {
      std::vector<std::string_view> names;
      names.reserve(term->args.size());
      for (const auto &argument : term->args)
        names.push_back(argument.name);
      if (!tensorOperationArgumentOrder(*operation, names) ||
          !std::all_of(term->args.begin(), term->args.end(),
                       [&](const Arg &argument) {
                         return isDirectDeterministicExpression(
                             argument.value, definedSymbols, context);
                       }))
        return false;
      return true;
    }
    if (const auto operation = numericOperationForName(term->name)) {
      return term->args.size() == numericOperationArity(*operation) &&
             std::all_of(term->args.begin(), term->args.end(),
                         [&](const Arg &argument) {
                           return argument.name.empty() &&
                                  isDirectDeterministicExpression(
                                      argument.value, definedSymbols, context);
                         });
    }
    if (term->name == "MOD") {
      return term->args.size() == 2 &&
             std::all_of(term->args.begin(), term->args.end(),
                         [&](const Arg &argument) {
                           return argument.name.empty() &&
                                  isDirectDeterministicExpression(
                                      argument.value, definedSymbols, context);
                         });
    }
    if (semanticOperationForName(term->name)) {
      return term->args.size() <= 255 &&
             std::all_of(term->args.begin(), term->args.end(),
                         [&](const Arg &argument) {
                           return isDirectDeterministicExpression(
                               argument.value, definedSymbols, context);
                         });
    }
    const bool fuzzyIntrinsic =
        term->nameId == symbolIdForName("similarity") ||
        term->nameId == symbolIdForName("membership") ||
        term->nameId == symbolIdForName("isA") ||
        term->nameId == symbolIdForName("commonAncestors") ||
        term->nameId == symbolIdForName("lowestCommonAncestor") ||
        term->nameId == symbolIdForName("highestCommonAncestor");
    if (fuzzyIntrinsic) {
      const auto expected =
          term->nameId == symbolIdForName("similarity") ? 2u : 2u;
      return term->args.size() == expected &&
             std::all_of(term->args.begin(), term->args.end(),
                         [&](const Arg &argument) {
                           return isDirectDeterministicExpression(
                               argument.value, definedSymbols, context);
                         });
    }
    if (term->nameId == symbolIdForName("temporalRank")) {
      return term->args.size() == 2 &&
             std::all_of(
                 term->args.begin(), term->args.end(), [](const Arg &argument) {
                   if (std::dynamic_pointer_cast<VarExpr>(argument.value))
                     return true;
                   const auto access =
                       std::dynamic_pointer_cast<AccessExpr>(argument.value);
                   return access &&
                          static_cast<bool>(std::dynamic_pointer_cast<VarExpr>(
                              access->target));
                 });
    }
    if (term->nameId == symbolIdForName("for_each_fact")) {
      // The third argument (a captures map) is optional: only
      // FactQueryNormalizer::iteration()'s generated calls provide one, for
      // predicates closing over an outer-scope value; a raw, hand-written
      // for_each_fact(type, callback) call stays exactly as it was.
      if (term->args.size() != 2 && term->args.size() != 3)
        return false;
      const auto type = std::dynamic_pointer_cast<VarExpr>(term->args[0].value);
      const auto callback =
          std::dynamic_pointer_cast<VarExpr>(term->args[1].value);
      const bool declaredType =
          type && context.factTypes.contains(type->nameId);
      if (!declaredType || !callback ||
          !context.procedures.contains(callback->nameId)) {
        return false;
      }
      return term->args.size() == 2 ||
             isDirectDeterministicExpression(term->args[2].value,
                                             definedSymbols, context);
    }
    const bool hasNamedFields =
        !term->args.empty() &&
        std::all_of(term->args.begin(), term->args.end(),
                    [](const Arg &argument) { return !argument.name.empty(); });
    // A capitalized, named term is an ordinary fact value even when its
    // schema was not declared in this module. The parser preserves that
    // distinction in TermExpr::isCapitalized; it is not a string heuristic
    // or an alternate source parser.
    const bool isFactValue =
        context.factTypes.contains(term->nameId) ||
        (term->isCapitalized && term->name.find('.') == std::string::npos &&
         hasNamedFields);
    const bool isProcedure = context.procedures.contains(term->nameId);
    return (isProcedure || isFactValue) &&
           std::all_of(term->args.begin(), term->args.end(),
                       [&](const Arg &argument) {
                         return isDirectDeterministicExpression(
                             argument.value, definedSymbols, context);
                       });
  }
  return false; // calls, lambdas, fact selections, and AST-only values
}

const SourceSpan &
firstUnsupportedExpression(const std::shared_ptr<Expr> &expression,
                           const std::unordered_set<SymbolId> &definedSymbols,
                           const DirectCompileContext &context) {
  if (!expression) {
    static const SourceSpan unknown{};
    return unknown;
  }
  if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(expression)) {
    for (const auto &item : array->items) {
      if (!isDirectDeterministicExpression(item, definedSymbols, context))
        return firstUnsupportedExpression(item, definedSymbols, context);
    }
  } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(expression)) {
    for (const auto &item : map->entries) {
      if (!isDirectDeterministicExpression(item.value, definedSymbols, context))
        return firstUnsupportedExpression(item.value, definedSymbols, context);
    }
  } else if (const auto access =
                 std::dynamic_pointer_cast<AccessExpr>(expression)) {
    if (!isDirectDeterministicExpression(access->target, definedSymbols,
                                         context))
      return firstUnsupportedExpression(access->target, definedSymbols,
                                        context);
  } else if (const auto operation =
                 std::dynamic_pointer_cast<OperatorExpression>(expression)) {
    if (operation->coreOperator == CoreOperator::Then &&
        operation->captureCount() == 2) {
      if (!isDirectDeterministicExpression(operation->capture(0),
                                           definedSymbols, context)) {
        return firstUnsupportedExpression(operation->capture(0), definedSymbols,
                                          context);
      }
      auto pipelineSymbols = definedSymbols;
      pipelineSymbols.insert(symbolIdForName("system.result"));
      if (!isDirectDeterministicExpression(operation->capture(1),
                                           pipelineSymbols, context)) {
        return firstUnsupportedExpression(operation->capture(1),
                                          pipelineSymbols, context);
      }
      return expression->sourceSpan;
    }
    if (operation->coreOperator == CoreOperator::Unknown &&
        (operation->resolvedMethodId == 0 ||
         !context.procedures.contains(operation->resolvedMethodId)))
      return expression->sourceSpan;
    for (std::size_t index = 0; index < operation->captureCount(); ++index) {
      const auto captured = operation->capture(index);
      if (!isDirectDeterministicExpression(captured, definedSymbols, context))
        return firstUnsupportedExpression(captured, definedSymbols, context);
    }
  } else if (const auto term =
                 std::dynamic_pointer_cast<TermExpr>(expression)) {
    if (builtinOperationArity(term->builtinId)) {
      for (const auto &argument : term->args) {
        if (!isDirectDeterministicExpression(argument.value, definedSymbols,
                                             context))
          return firstUnsupportedExpression(argument.value, definedSymbols,
                                            context);
      }
      return expression->sourceSpan;
    }
    if (tensorOperationForName(term->name)) {
      for (const auto &argument : term->args) {
        if (!isDirectDeterministicExpression(argument.value, definedSymbols,
                                             context))
          return firstUnsupportedExpression(argument.value, definedSymbols,
                                            context);
      }
      return expression->sourceSpan;
    }
    if (numericOperationForName(term->name) || term->name == "MOD") {
      for (const auto &argument : term->args) {
        if (!argument.name.empty() ||
            !isDirectDeterministicExpression(argument.value, definedSymbols,
                                             context)) {
          return argument.value ? argument.value->sourceSpan
                                : expression->sourceSpan;
        }
      }
      return expression->sourceSpan;
    }
    if (semanticOperationForName(term->name)) {
      for (const auto &argument : term->args) {
        if (!isDirectDeterministicExpression(argument.value, definedSymbols,
                                             context))
          return firstUnsupportedExpression(argument.value, definedSymbols,
                                            context);
      }
      return expression->sourceSpan;
    }
    if ((term->nameId == symbolIdForName("similarity") ||
         term->nameId == symbolIdForName("membership") ||
         term->nameId == symbolIdForName("isA") ||
         term->nameId == symbolIdForName("commonAncestors") ||
         term->nameId == symbolIdForName("lowestCommonAncestor") ||
         term->nameId == symbolIdForName("highestCommonAncestor")) &&
        term->args.size() == 2) {
      for (const auto &argument : term->args) {
        if (!isDirectDeterministicExpression(argument.value, definedSymbols,
                                             context))
          return firstUnsupportedExpression(argument.value, definedSymbols,
                                            context);
      }
      return expression->sourceSpan;
    }
    if (term->nameId == symbolIdForName("temporalRank")) {
      for (const auto &argument : term->args) {
        if (!isDirectDeterministicExpression(argument.value, definedSymbols,
                                             context)) {
          return firstUnsupportedExpression(argument.value, definedSymbols,
                                            context);
        }
      }
      return expression->sourceSpan;
    }
    if (term->nameId == symbolIdForName("for_each_fact"))
      return expression->sourceSpan;
    const bool isProcedure = context.procedures.contains(term->nameId);
    const bool isFactType = context.factTypes.contains(term->nameId);
    if (!isProcedure && !isFactType) {
      return expression->sourceSpan;
    }
    for (const auto &argument : term->args) {
      if (!isDirectDeterministicExpression(argument.value, definedSymbols,
                                           context))
        return firstUnsupportedExpression(argument.value, definedSymbols,
                                          context);
    }
  }
  return expression->sourceSpan;
}

bool isDirectReturn(const std::shared_ptr<Goal> &goal,
                    const std::unordered_set<SymbolId> &definedSymbols,
                    const DirectCompileContext &context) {
  const auto returned = std::dynamic_pointer_cast<ReturnGoal>(goal);
  return returned && std::all_of(returned->fields.begin(),
                                 returned->fields.end(), [&](const Arg &field) {
                                   return isDirectDeterministicExpression(
                                       field.value, definedSymbols, context);
                                 });
}

bool isDirectCondition(const std::shared_ptr<Goal> &goal,
                       const std::unordered_set<SymbolId> &definedSymbols,
                       const DirectCompileContext &context) {
  const auto comparison = std::dynamic_pointer_cast<BinaryGoal>(goal);
  if (!comparison ||
      !isDirectDeterministicExpression(comparison->left, definedSymbols,
                                       context) ||
      !isDirectDeterministicExpression(comparison->right, definedSymbols,
                                       context))
    return false;
  switch (comparison->op) {
  case TokenId::EQUAL:
  case TokenId::NOT_EQUAL:
  case TokenId::LESS:
  case TokenId::LESS_EQUAL:
  case TokenId::GREATER:
  case TokenId::GREATER_EQUAL:
    return true;
  default:
    return false;
  }
}

bool isDirectGoalSequence(const std::vector<std::shared_ptr<Goal>> &goals,
                          std::unordered_set<SymbolId> visible,
                          const DirectCompileContext &context) {
  if (goals.empty())
    return false;
  for (std::size_t index = 0; index + 1 < goals.size(); ++index) {
    const auto assignment = std::dynamic_pointer_cast<AssignGoal>(goals[index]);
    if (!assignment || !assignment->expr ||
        visible.contains(assignment->nameId) ||
        !isDirectDeterministicExpression(assignment->expr, visible, context))
      return false;
    visible.insert(assignment->nameId);
  }
  if (isDirectReturn(goals.back(), visible, context))
    return true;
  const auto conditional = std::dynamic_pointer_cast<IfGoal>(goals.back());
  return conditional &&
         isDirectCondition(conditional->condition, visible, context) &&
         isDirectGoalSequence(conditional->thenBranch, visible, context) &&
         isDirectGoalSequence(conditional->elseBranch, visible, context);
}

const SourceSpan &
firstUnsupportedGoalSequence(const std::vector<std::shared_ptr<Goal>> &goals,
                             std::unordered_set<SymbolId> visible,
                             const DirectCompileContext &context) {
  static const SourceSpan unknown{};
  if (goals.empty())
    return unknown;
  for (std::size_t index = 0; index + 1 < goals.size(); ++index) {
    const auto assignment = std::dynamic_pointer_cast<AssignGoal>(goals[index]);
    if (!assignment || !assignment->expr ||
        visible.contains(assignment->nameId)) {
      return goals[index] ? goals[index]->sourceSpan : unknown;
    }
    if (!isDirectDeterministicExpression(assignment->expr, visible, context)) {
      return firstUnsupportedExpression(assignment->expr, visible, context);
    }
    visible.insert(assignment->nameId);
  }
  const auto returned = std::dynamic_pointer_cast<ReturnGoal>(goals.back());
  if (!returned) {
    const auto conditional = std::dynamic_pointer_cast<IfGoal>(goals.back());
    if (!conditional)
      return goals.back() ? goals.back()->sourceSpan : unknown;
    if (!isDirectCondition(conditional->condition, visible, context))
      return conditional->condition ? conditional->condition->sourceSpan
                                    : conditional->sourceSpan;
    if (!isDirectGoalSequence(conditional->thenBranch, visible, context))
      return firstUnsupportedGoalSequence(conditional->thenBranch, visible,
                                          context);
    return firstUnsupportedGoalSequence(conditional->elseBranch, visible,
                                        context);
  }
  for (const auto &field : returned->fields) {
    if (!isDirectDeterministicExpression(field.value, visible, context)) {
      return firstUnsupportedExpression(field.value, visible, context);
    }
  }
  return returned->sourceSpan;
}

const SourceSpan &
firstUnsupportedProcedure(const ClauseStmt &method,
                          const std::unordered_set<SymbolId> &globals,
                          const DirectCompileContext &context) {
  auto visible = globals;
  for (const auto parameter : procedureParameters(method))
    visible.insert(parameter);
  if (method.body.size() == 1) {
    if (const auto conditional =
            std::dynamic_pointer_cast<IfGoal>(method.body.front())) {
      if (!isDirectCondition(conditional->condition, visible, context)) {
        const auto comparison =
            std::dynamic_pointer_cast<BinaryGoal>(conditional->condition);
        if (comparison) {
          if (!isDirectDeterministicExpression(comparison->left, visible,
                                               context))
            return firstUnsupportedExpression(comparison->left, visible,
                                              context);
          if (!isDirectDeterministicExpression(comparison->right, visible,
                                               context))
            return firstUnsupportedExpression(comparison->right, visible,
                                              context);
        }
        return conditional->condition ? conditional->condition->sourceSpan
                                      : method.sourceSpan;
      }
      if (!isDirectGoalSequence(conditional->thenBranch, visible, context))
        return firstUnsupportedGoalSequence(conditional->thenBranch, visible,
                                            context);
      return firstUnsupportedGoalSequence(conditional->elseBranch, visible,
                                          context);
    }
  }
  return firstUnsupportedGoalSequence(method.body, std::move(visible), context);
}

bool isDirectBody(const ClauseStmt &method,
                  const std::unordered_set<SymbolId> &visible,
                  const DirectCompileContext &context) {
  if (method.body.empty() || !method.fallbackBranches.empty())
    return false;
  if (method.body.size() == 1 &&
      isDirectReturn(method.body.front(), visible, context))
    return true;
  if (method.body.size() == 1) {
    const auto conditional =
        std::dynamic_pointer_cast<IfGoal>(method.body.front());
    return conditional &&
           isDirectCondition(conditional->condition, visible, context) &&
           isDirectGoalSequence(conditional->thenBranch, visible, context) &&
           isDirectGoalSequence(conditional->elseBranch, visible, context);
  }
  return isDirectGoalSequence(method.body, visible, context);
}

bool isDirectEntry(const ClauseStmt &method,
                   const std::unordered_set<SymbolId> &definedSymbols,
                   const DirectCompileContext &context) {
  return method.head.nameId == symbolIdForName("main") &&
         method.head.args.empty() &&
         isDirectBody(method, definedSymbols, context);
}

bool isDirectProcedure(const ClauseStmt &method,
                       const std::unordered_set<SymbolId> &globals,
                       const DirectCompileContext &context) {
  auto visible = globals;
  for (const auto parameter : procedureParameters(method)) {
    if (!visible.insert(parameter).second)
      return false;
  }
  return isDirectBody(method, visible, context);
}

// Each frontend lowering fragment owns its tables and registers.  This linker
// preserves canonical integer IR while rebasing every table/register/jump
// operand; it never reuses AST nodes at runtime. Instruction decoding below
// checks local widths while the one final module verifier owns semantic and
// control-flow validation, avoiding repeated whole-fragment verification.
void appendFragment(FelidaeIr &target, FelidaeIr fragment,
                    bool dropTerminalReturn, bool dropTerminalEnd = false) {
  if (dropTerminalReturn) {
    if (fragment.words.size() < 4 ||
        fragment.words[fragment.words.size() - 4] !=
            static_cast<IrWord>(IrOpcode::Return) ||
        fragment.words.back() != static_cast<IrWord>(IrOpcode::End)) {
      throw IntegerParserError("IR fragment does not have a terminal return");
    }
    fragment.words.resize(fragment.words.size() - 4);
  } else if (dropTerminalEnd) {
    if (fragment.words.empty() ||
        fragment.words.back() != static_cast<IrWord>(IrOpcode::End)) {
      throw IntegerParserError("IR fragment is missing END");
    }
    fragment.words.pop_back();
  }
  const IrWord registerBase = target.registerCount;
  const IrWord constantBase = target.constants.size();
  const IrWord textBase = target.texts.size();
  const IrWord symbolBase = target.symbols.size();
  const IrWord wordBase = target.words.size();
  for (std::size_t index = 0; index < fragment.constants.size(); ++index) {
    auto constant = fragment.constants[index];
    if (constant.kind == IrConstantKind::Text) {
      constant.value += textBase;
    }
    target.constants.push_back(constant);
  }
  target.texts.insert(target.texts.end(), fragment.texts.begin(),
                      fragment.texts.end());
  target.symbols.insert(target.symbols.end(), fragment.symbols.begin(),
                        fragment.symbols.end());
  auto reg = [&](IrWord &value) { value += registerBase; };
  auto symbol = [&](IrWord &value) { value += symbolBase; };
  for (std::size_t pc = 0; pc < fragment.words.size();) {
    const auto opcode = static_cast<IrOpcode>(fragment.words[pc]);
    std::size_t width = 0;
    switch (opcode) {
    case IrOpcode::End:
      width = 1;
      break;
    case IrOpcode::Jump:
      fragment.words[pc + 1] += wordBase;
      width = 2;
      break;
    case IrOpcode::LoadConst:
      reg(fragment.words[pc + 1]);
      fragment.words[pc + 2] += constantBase;
      width = 3;
      break;
    case IrOpcode::LoadSymbol:
      reg(fragment.words[pc + 1]);
      symbol(fragment.words[pc + 2]);
      width = 3;
      break;
    case IrOpcode::StoreSymbol:
      symbol(fragment.words[pc + 1]);
      reg(fragment.words[pc + 2]);
      width = 3;
      break;
    case IrOpcode::Move:
      reg(fragment.words[pc + 1]);
      reg(fragment.words[pc + 2]);
      width = 3;
      break;
    case IrOpcode::JumpIfFalse:
      reg(fragment.words[pc + 1]);
      fragment.words[pc + 2] += wordBase;
      width = 3;
      break;
    case IrOpcode::MakeFact:
      reg(fragment.words[pc + 1]);
      symbol(fragment.words[pc + 2]);
      width = 3;
      break;
    case IrOpcode::Return:
      reg(fragment.words[pc + 1]);
      width = 3;
      break;
    case IrOpcode::Add:
    case IrOpcode::Sub:
    case IrOpcode::Mul:
    case IrOpcode::Div:
    case IrOpcode::Mod:
    case IrOpcode::Similarity:
    case IrOpcode::HierarchyIsA:
    case IrOpcode::HierarchyCommonAncestors:
    case IrOpcode::HierarchyLeastCommonAncestors:
    case IrOpcode::HierarchyMostGeneralAncestors:
      reg(fragment.words[pc + 1]);
      reg(fragment.words[pc + 2]);
      reg(fragment.words[pc + 3]);
      width = 4;
      break;
    case IrOpcode::TemporalRank:
      reg(fragment.words[pc + 1]);
      symbol(fragment.words[pc + 2]);
      symbol(fragment.words[pc + 3]);
      width = 4;
      break;
    case IrOpcode::Membership:
      for (std::size_t index = 1; index < 6; ++index) {
        reg(fragment.words[pc + index]);
      }
      width = 6;
      break;
    case IrOpcode::Compare:
      reg(fragment.words[pc + 1]);
      reg(fragment.words[pc + 2]);
      reg(fragment.words[pc + 3]);
      width = 5;
      break;
    case IrOpcode::GetField:
      reg(fragment.words[pc + 1]);
      reg(fragment.words[pc + 2]);
      symbol(fragment.words[pc + 3]);
      width = 4;
      break;
    case IrOpcode::SetField:
      reg(fragment.words[pc + 1]);
      symbol(fragment.words[pc + 2]);
      reg(fragment.words[pc + 3]);
      width = 4;
      break;
    case IrOpcode::ForEachFact:
      reg(fragment.words[pc + 1]);
      symbol(fragment.words[pc + 2]);
      symbol(fragment.words[pc + 3]);
      reg(fragment.words[pc + 4]);
      width = 5;
      break;
    case IrOpcode::Call:
    case IrOpcode::Builtin:
    case IrOpcode::SemanticEval:
    case IrOpcode::Numeric:
    case IrOpcode::Tensor:
    case IrOpcode::MakeArray: {
      reg(fragment.words[pc + 1]);
      if (opcode == IrOpcode::Call)
        symbol(fragment.words[pc + 2]);
      const auto count = fragment.words[pc + 3];
      for (std::size_t index = 0; index < count; ++index)
        reg(fragment.words[pc + 4 + index]);
      width = 4 + count;
      break;
    }
    case IrOpcode::CallNamed:
    case IrOpcode::MakeMap: {
      reg(fragment.words[pc + 1]);
      if (opcode == IrOpcode::CallNamed)
        symbol(fragment.words[pc + 2]);
      const auto count = fragment.words[pc + 3];
      for (std::size_t index = 0; index < count; ++index) {
        auto &key = fragment.words[pc + 4 + index * 2];
        if (opcode == IrOpcode::CallNamed) {
          if (key != 0)
            key += symbolBase;
        } else
          symbol(key);
        reg(fragment.words[pc + 5 + index * 2]);
      }
      width = 4 + count * 2;
      break;
    }
    case IrOpcode::Count:
      throw IntegerParserError("IR fragment has an invalid opcode");
    }
    pc += width;
  }
  for (auto entry : fragment.sourceMap) {
    entry.instructionWord += wordBase;
    target.sourceMap.push_back(std::move(entry));
  }
  target.words.insert(target.words.end(), fragment.words.begin(),
                      fragment.words.end());
  target.registerCount += fragment.registerCount;
}

CoreOperator directConditionOperator(TokenId::Id token) {
  switch (token) {
  case TokenId::EQUAL:
    return CoreOperator::StrictEqual;
  case TokenId::NOT_EQUAL:
    return CoreOperator::StrictNotEqual;
  case TokenId::LESS:
    return CoreOperator::Less;
  case TokenId::LESS_EQUAL:
    return CoreOperator::LessEqual;
  case TokenId::GREATER:
    return CoreOperator::Greater;
  case TokenId::GREATER_EQUAL:
    return CoreOperator::GreaterEqual;
  default:
    throw IntegerParserError("condition has no direct IR comparison");
  }
}

FelidaeIr compileDirectProcedure(
    const ClauseStmt &method, const std::unordered_set<SymbolId> &factTypes);

FelidaeIr compileDirectConditionalEntry(
    const ClauseStmt &method, const std::unordered_set<SymbolId> &factTypes) {
  const auto conditional =
      std::dynamic_pointer_cast<IfGoal>(method.body.front());
  const auto comparison =
      std::dynamic_pointer_cast<BinaryGoal>(conditional->condition);
  auto expression = std::make_shared<OperatorExpression>(
      directConditionOperator(comparison->op), comparison->left,
      comparison->right);
  expression->sourceSpan = comparison->sourceSpan;
  auto conditionIr = IrCodeGenerator::lowerExpression(expression, factTypes);
  const auto conditionRegister =
      conditionIr.words.at(conditionIr.words.size() - 3);
  FelidaeIr result;
  appendFragment(result, std::move(conditionIr), true);
  const auto elseJump = result.words.size();
  result.words.insert(
      result.words.end(),
      {static_cast<IrWord>(IrOpcode::JumpIfFalse), conditionRegister, 0});
  ClauseStmt thenMethod(method.head, conditional->thenBranch);
  appendFragment(
      result, compileDirectProcedure(thenMethod, factTypes),
      false, true);
  const auto endJump = result.words.size();
  result.words.insert(result.words.end(),
                      {static_cast<IrWord>(IrOpcode::Jump), 0});
  const auto elseTarget = result.words.size();
  ClauseStmt elseMethod(method.head, conditional->elseBranch);
  appendFragment(
      result, compileDirectProcedure(elseMethod, factTypes),
      false);
  result.words[elseJump + 2] = elseTarget;
  result.words[endJump + 1] = result.words.size() - 1; // the final END boundary
  return result;
}

FelidaeIr compileDirectSequentialEntry(
    const ClauseStmt &method, const std::unordered_set<SymbolId> &factTypes) {
  FelidaeIr result;
  for (std::size_t index = 0; index + 1 < method.body.size(); ++index) {
    const auto assignment =
        std::dynamic_pointer_cast<AssignGoal>(method.body[index]);
    GlobalBindingStmt binding(assignment->name, assignment->expr);
    binding.sourceSpan = assignment->sourceSpan;
    appendFragment(result,
                   IrCodeGenerator::lowerGlobalBinding(binding, factTypes),
                   true);
  }
  // The final statement is not necessarily a return goal -- isDirectProcedure
  // also accepts a trailing IfGoal after a run of assignments. Recurse into
  // compileDirectProcedure, which already dispatches a single-statement body
  // to compileDirectConditionalEntry when it's an IfGoal and to
  // lowerEntryMethod (return-goal-only) otherwise, instead of assuming
  // the return-goal case here and rejecting a compiler-approved program.
  ClauseStmt returned(method.head, {method.body.back()});
  appendFragment(
      result, compileDirectProcedure(returned, factTypes),
      false);
  return result;
}

FelidaeIr compileDirectProcedure(
    const ClauseStmt &method, const std::unordered_set<SymbolId> &factTypes) {
  if (method.body.size() == 1) {
    if (std::dynamic_pointer_cast<IfGoal>(method.body.front())) {
      return compileDirectConditionalEntry(method, factTypes);
    }
    return IrCodeGenerator::lowerEntryMethod(method, factTypes);
  }
  return compileDirectSequentialEntry(method, factTypes);
}

} // namespace

FelidaeIr IrCodeGenerator::lowerExpression(
    const std::shared_ptr<Expr> &expression,
    const std::unordered_set<SymbolId> &factTypes) {
  FelidaeIr ir;
  const auto addNumber = [&](double number) {
    ir.constants.push_back({IrConstantKind::Number, encodeIrNumber(number)});
    return static_cast<IrWord>(ir.constants.size() - 1);
  };
  const auto addBoolean = [&](bool boolean) {
    ir.constants.push_back({IrConstantKind::Boolean, boolean ? 1ull : 0ull});
    return static_cast<IrWord>(ir.constants.size() - 1);
  };
  const auto addText = [&](const StringExpr &text) {
    if (text.containsEscape) {
      throw IntegerParserError("string literal cannot be lowered without its "
                               "original SentencePiece IDs");
    }
    ir.texts.push_back(text.sentencePieceIds);
    ir.constants.push_back(
        {IrConstantKind::Text, static_cast<IrWord>(ir.texts.size() - 1)});
    return static_cast<IrWord>(ir.constants.size() - 1);
  };
  const auto addNil = [&]() {
    ir.constants.push_back({IrConstantKind::Nil, 0});
    return static_cast<IrWord>(ir.constants.size() - 1);
  };
  std::optional<RegisterId> pipelineResult;
  const auto emitFactFields =
      [&](RegisterId fact,
          const std::vector<std::pair<SymbolId, RegisterId>> &fields) {
        std::vector<std::pair<SymbolId, std::vector<RegisterId>>> grouped;
        std::unordered_map<SymbolId, std::size_t> groupIndexes;
        grouped.reserve(fields.size());
        groupIndexes.reserve(fields.size());
        for (const auto &[name, value] : fields) {
          const auto [position, inserted] =
              groupIndexes.emplace(name, grouped.size());
          if (inserted)
            grouped.push_back({name, {value}});
          else
            grouped[position->second].second.push_back(value);
        }
        for (const auto &[name, values] : grouped) {
          RegisterId value = values.front();
          if (values.size() > 1) {
            value = static_cast<RegisterId>(ir.registerCount++);
            ir.words.insert(ir.words.end(),
                            {static_cast<IrWord>(IrOpcode::MakeArray), value, 0,
                             static_cast<IrWord>(values.size())});
            ir.words.insert(ir.words.end(), values.begin(), values.end());
          }
          ir.symbols.push_back(name);
          ir.words.insert(ir.words.end(),
                          {static_cast<IrWord>(IrOpcode::SetField), fact,
                           static_cast<IrWord>(ir.symbols.size() - 1), value});
        }
      };
  auto lower = [&](auto &&self,
                   const std::shared_ptr<Expr> &value) -> RegisterId {
    const auto emittedAt = ir.words.size();
    const auto result = static_cast<RegisterId>(ir.registerCount++);
    if (const auto number = std::dynamic_pointer_cast<NumberExpr>(value)) {
      ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadConst),
                                       result, addNumber(number->value)});
    } else if (const auto boolean =
                   std::dynamic_pointer_cast<BoolExpr>(value)) {
      ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadConst),
                                       result, addBoolean(boolean->value)});
    } else if (const auto text = std::dynamic_pointer_cast<StringExpr>(value)) {
      ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadConst),
                                       result, addText(*text)});
    } else if (std::dynamic_pointer_cast<NilExpr>(value)) {
      ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::LoadConst),
                                       result, addNil()});
    } else if (const auto variable =
                   std::dynamic_pointer_cast<VarExpr>(value)) {
      if (variable->nameId == symbolIdForName("system.result")) {
        if (!pipelineResult) {
          throw IntegerParserError(
              "system.result is available only on the right side of 'then'");
        }
        ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::Move),
                                         result, *pipelineResult});
        return result;
      }
      ir.symbols.push_back(variable->nameId);
      ir.words.insert(ir.words.end(),
                      {static_cast<IrWord>(IrOpcode::LoadSymbol), result,
                       static_cast<IrWord>(ir.symbols.size() - 1)});
    } else if (const auto term = std::dynamic_pointer_cast<TermExpr>(value)) {
      if (const auto arity = builtinOperationArity(term->builtinId)) {
        std::vector<std::string_view> names;
        names.reserve(term->args.size());
        for (const auto &argument : term->args)
          names.push_back(argument.name);
        const auto order =
            builtinOperationArgumentOrder(term->builtinId, names);
        if (!order)
          throw IntegerParserError(term->name +
                                   " has invalid builtin arguments");
        std::vector<std::optional<RegisterId>> ordered(*arity);
        for (std::size_t index = 0; index < term->args.size(); ++index)
          ordered[(*order)[index]] = self(self, term->args[index].value);
        std::vector<RegisterId> operands;
        operands.reserve(*arity);
        for (const auto operand : ordered) {
          if (!operand)
            throw IntegerParserError(term->name + " omits a builtin argument");
          operands.push_back(*operand);
        }
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::Builtin), result,
                         static_cast<IrWord>(term->builtinId),
                         static_cast<IrWord>(operands.size())});
        ir.words.insert(ir.words.end(), operands.begin(), operands.end());
      } else if (const auto operation = tensorOperationForName(term->name)) {
        const auto arity = tensorOperationArity(*operation);
        std::vector<std::string_view> names;
        names.reserve(term->args.size());
        for (const auto &argument : term->args)
          names.push_back(argument.name);
        const auto order = tensorOperationArgumentOrder(*operation, names);
        if (!order)
          throw IntegerParserError(term->name +
                                   " has invalid tensor arguments");
        std::vector<std::optional<RegisterId>> ordered(arity);
        for (std::size_t index = 0; index < arity; ++index) {
          const auto &argument = term->args[index];
          ordered[(*order)[index]] = self(self, argument.value);
        }
        std::vector<RegisterId> operands;
        operands.reserve(arity);
        for (const auto operand : ordered) {
          if (!operand)
            throw IntegerParserError(term->name + " omits a tensor argument");
          operands.push_back(*operand);
        }
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::Tensor), result,
                         static_cast<IrWord>(*operation),
                         static_cast<IrWord>(operands.size())});
        ir.words.insert(ir.words.end(), operands.begin(), operands.end());
      } else if (const auto operation = numericOperationForName(term->name)) {
        const auto arity = numericOperationArity(*operation);
        if (term->args.size() != arity ||
            std::any_of(
                term->args.begin(), term->args.end(),
                [](const Arg &argument) { return !argument.name.empty(); })) {
          throw IntegerParserError(term->name + " requires exactly " +
                                   std::to_string(arity) +
                                   " positional numeric arguments");
        }
        std::vector<RegisterId> operands;
        operands.reserve(arity);
        for (const auto &argument : term->args)
          operands.push_back(self(self, argument.value));
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::Numeric), result,
                         static_cast<IrWord>(*operation),
                         static_cast<IrWord>(operands.size())});
        ir.words.insert(ir.words.end(), operands.begin(), operands.end());
      } else if (term->name == "MOD") {
        if (term->args.size() != 2 ||
            std::any_of(
                term->args.begin(), term->args.end(),
                [](const Arg &argument) { return !argument.name.empty(); })) {
          throw IntegerParserError(
              "MOD requires exactly two positional numeric arguments");
        }
        const auto left = self(self, term->args[0].value);
        const auto right = self(self, term->args[1].value);
        ir.words.insert(ir.words.end(), {static_cast<IrWord>(IrOpcode::Mod),
                                         result, left, right});
      } else if (const auto operation = semanticOperationForName(term->name)) {
        if (term->args.size() > 255)
          throw IntegerParserError("semantic operation has too many inputs");
        std::vector<RegisterId> inputs;
        inputs.reserve(term->args.size());
        for (const auto &argument : term->args)
          inputs.push_back(self(self, argument.value));
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::SemanticEval), result,
                         static_cast<IrWord>(*operation),
                         static_cast<IrWord>(inputs.size())});
        ir.words.insert(ir.words.end(), inputs.begin(), inputs.end());
      } else if (term->nameId == symbolIdForName("for_each_fact")) {
        // The third argument (a captures map) is optional -- see the
        // isDirectDeterministicExpression case above and
        // FactQueryNormalizer::iteration() in this file. A raw two-argument
        // call gets an empty map so IrOpcode::ForEachFact's fifth operand
        // (see RegisterVm.cpp) is always a valid register either way.
        if (term->args.size() != 2 && term->args.size() != 3) {
          throw IntegerParserError(
              "for_each_fact requires a fact type and callback");
        }
        const auto type =
            std::dynamic_pointer_cast<VarExpr>(term->args[0].value);
        const auto callback =
            std::dynamic_pointer_cast<VarExpr>(term->args[1].value);
        const auto typeId =
            type && factTypes.contains(type->nameId) ? type->nameId : 0;
        if (!type || !callback || typeId == 0) {
          throw IntegerParserError("for_each_fact requires a declared fact "
                                   "type and deterministic callback");
        }
        const std::shared_ptr<Expr> capturesExpr =
            term->args.size() == 3
                ? term->args[2].value
                : std::make_shared<MapExpr>(std::vector<MapEntry>{});
        const auto capturesRegister = self(self, capturesExpr);
        ir.symbols.push_back(typeId);
        const auto typeSymbol = static_cast<IrWord>(ir.symbols.size() - 1);
        ir.symbols.push_back(callback->nameId);
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::ForEachFact), result,
                         typeSymbol,
                         static_cast<IrWord>(ir.symbols.size() - 1),
                         capturesRegister});
      } else if (term->nameId == symbolIdForName("similarity")) {
        if (term->args.size() != 2)
          throw IntegerParserError("similarity requires exactly two arguments");
        const auto left = self(self, term->args[0].value);
        const auto right = self(self, term->args[1].value);
        ir.words.insert(
            ir.words.end(),
            {static_cast<IrWord>(IrOpcode::Similarity), result, left, right});
      } else if (term->nameId == symbolIdForName("isA") ||
                 term->nameId == symbolIdForName("commonAncestors") ||
                 term->nameId == symbolIdForName("lowestCommonAncestor") ||
                 term->nameId == symbolIdForName("highestCommonAncestor")) {
        if (term->args.size() != 2) {
          throw IntegerParserError(term->name +
                                   " requires exactly two hierarchy arguments");
        }
        const auto left = self(self, term->args[0].value);
        const auto right = self(self, term->args[1].value);
        const auto opcode =
            term->nameId == symbolIdForName("isA") ? IrOpcode::HierarchyIsA
            : term->nameId == symbolIdForName("commonAncestors")
                ? IrOpcode::HierarchyCommonAncestors
            : term->nameId == symbolIdForName("lowestCommonAncestor")
                ? IrOpcode::HierarchyLeastCommonAncestors
                : IrOpcode::HierarchyMostGeneralAncestors;
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(opcode), result, left, right});
      } else if (term->nameId == symbolIdForName("temporalRank")) {
        if (term->args.size() != 2) {
          throw IntegerParserError(
              "temporalRank requires effective-at and priority field symbols");
        }
        const auto fieldSymbol = [&](const Arg &argument) {
          SymbolId fieldId = 0;
          if (const auto field =
                  std::dynamic_pointer_cast<VarExpr>(argument.value)) {
            fieldId = field->nameId;
          } else if (const auto access = std::dynamic_pointer_cast<AccessExpr>(
                         argument.value)) {
            const auto scope =
                std::dynamic_pointer_cast<VarExpr>(access->target);
            if (scope)
              fieldId = symbolIdForName(scope->name + "." + access->key);
          }
          if (fieldId == 0) {
            throw IntegerParserError(
                "temporalRank fields must be symbol names");
          }
          ir.symbols.push_back(fieldId);
          return static_cast<IrWord>(ir.symbols.size() - 1);
        };
        const auto effectiveAt = fieldSymbol(term->args[0]);
        const auto priority = fieldSymbol(term->args[1]);
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::TemporalRank), result,
                         effectiveAt, priority});
      } else if (term->nameId == symbolIdForName("membership")) {
        if (term->args.size() != 2)
          throw IntegerParserError(
              "membership requires a value and named profile");
        const auto valueRegister = self(self, term->args[0].value);
        const auto profile = self(self, term->args[1].value);
        const auto field = [&](const char *name) {
          const auto fieldRegister =
              static_cast<RegisterId>(ir.registerCount++);
          ir.symbols.push_back(symbolIdForName(name));
          ir.words.insert(ir.words.end(),
                          {static_cast<IrWord>(IrOpcode::GetField),
                           fieldRegister, profile,
                           static_cast<IrWord>(ir.symbols.size() - 1)});
          return fieldRegister;
        };
        const auto peak = field("peak");
        const auto fadesIn = field("fades_in");
        const auto fadesOut = field("fades_out");
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::Membership), result,
                         valueRegister, peak, fadesIn, fadesOut});
      } else {
        std::vector<RegisterId> arguments;
        arguments.reserve(term->args.size());
        const bool hasNamedArguments = std::any_of(
            term->args.begin(), term->args.end(),
            [](const Arg &argument) { return !argument.name.empty(); });
        std::vector<IrWord> names;
        names.reserve(term->args.size());
        for (const auto &argument : term->args) {
          arguments.push_back(self(self, argument.value));
          if (hasNamedArguments && argument.name.empty()) {
            names.push_back(0);
          } else if (hasNamedArguments) {
            ir.symbols.push_back(argument.nameId);
            names.push_back(static_cast<IrWord>(ir.symbols.size()));
          }
        }
        // Capitalized terms with named fields are first-class fact values
        // even without a separately declared fact schema. The AST carries
        // this grammar decision; lowering does not infer it from text.
        const bool factValue =
            factTypes.contains(term->nameId) ||
            (term->isCapitalized && term->name.find('.') == std::string::npos &&
             hasNamedArguments);
        if (factValue) {
          if (!hasNamedArguments) {
            throw IntegerParserError("fact construction requires named fields");
          }
          ir.symbols.push_back(term->nameId);
          ir.words.insert(ir.words.end(),
                          {static_cast<IrWord>(IrOpcode::MakeFact), result,
                           static_cast<IrWord>(ir.symbols.size() - 1)});
          std::vector<std::pair<SymbolId, RegisterId>> fields;
          fields.reserve(arguments.size());
          for (std::size_t index = 0; index < arguments.size(); ++index) {
            if (names[index] == 0)
              throw IntegerParserError(
                  "fact construction requires named fields");
            fields.emplace_back(term->args[index].nameId, arguments[index]);
          }
          emitFactFields(result, fields);
        } else {
          ir.symbols.push_back(term->nameId);
          if (!hasNamedArguments) {
            ir.words.insert(ir.words.end(),
                            {static_cast<IrWord>(IrOpcode::Call), result,
                             static_cast<IrWord>(ir.symbols.size() - 1),
                             static_cast<IrWord>(arguments.size())});
            ir.words.insert(ir.words.end(), arguments.begin(), arguments.end());
          } else {
            ir.words.insert(ir.words.end(),
                            {static_cast<IrWord>(IrOpcode::CallNamed), result,
                             static_cast<IrWord>(ir.symbols.size() - 1),
                             static_cast<IrWord>(arguments.size())});
            for (std::size_t index = 0; index < arguments.size(); ++index) {
              ir.words.push_back(names[index]);
              ir.words.push_back(arguments[index]);
            }
          }
        }
      }
    } else if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) {
      std::vector<RegisterId> items;
      items.reserve(array->items.size());
      for (const auto &item : array->items)
        items.push_back(self(self, item));
      ir.words.insert(ir.words.end(),
                      {static_cast<IrWord>(IrOpcode::MakeArray), result, 0,
                       static_cast<IrWord>(items.size())});
      ir.words.insert(ir.words.end(), items.begin(), items.end());
    } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(value)) {
      std::vector<std::pair<SymbolId, RegisterId>> entries;
      entries.reserve(map->entries.size());
      for (const auto &entry : map->entries) {
        const auto item = self(self, entry.value);
        entries.emplace_back(entry.keyId, item);
      }
      if (map->factType.empty()) {
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::MakeMap), result, 0,
                         static_cast<IrWord>(entries.size())});
        for (const auto &[field, item] : entries) {
          ir.symbols.push_back(field);
          ir.words.push_back(static_cast<IrWord>(ir.symbols.size() - 1));
          ir.words.push_back(item);
        }
      } else {
        ir.symbols.push_back(symbolIdForName(map->factType));
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::MakeFact), result,
                         static_cast<IrWord>(ir.symbols.size() - 1)});
        emitFactFields(result, entries);
      }
    } else if (const auto access =
                   std::dynamic_pointer_cast<AccessExpr>(value)) {
      const auto target = self(self, access->target);
      ir.symbols.push_back(access->keyId);
      ir.words.insert(ir.words.end(),
                      {static_cast<IrWord>(IrOpcode::GetField), result, target,
                       static_cast<IrWord>(ir.symbols.size() - 1)});
    } else if (const auto operation =
                   std::dynamic_pointer_cast<OperatorExpression>(value)) {
      const auto core = operation->coreOperator;
      if (core == CoreOperator::Unknown) {
        if (operation->resolvedMethodId == 0) {
          throw IntegerParserError(
              "mixfix expression requires verified model target selection");
        }
        std::vector<RegisterId> arguments;
        arguments.reserve(operation->captureCount());
        for (std::size_t index = 0; index < operation->captureCount();
             ++index) {
          arguments.push_back(self(self, operation->capture(index)));
        }
        ir.symbols.push_back(operation->resolvedMethodId);
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::Call), result,
                         static_cast<IrWord>(ir.symbols.size() - 1),
                         static_cast<IrWord>(arguments.size())});
        ir.words.insert(ir.words.end(), arguments.begin(), arguments.end());
      } else if (core == CoreOperator::UnaryPlus) {
        return self(self, operation->capture(0));
      } else if (core == CoreOperator::Then) {
        if (operation->captureCount() != 2) {
          throw IntegerParserError(
              "then pipeline requires a left and right expression");
        }
        const auto left = self(self, operation->capture(0));
        const auto previousPipelineResult = pipelineResult;
        pipelineResult = left;
        const auto right = self(self, operation->capture(1));
        pipelineResult = previousPipelineResult;
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::Move), result, right});
      } else if (core == CoreOperator::UnaryMinus) {
        const auto source = self(self, operation->capture(0));
        const auto zero = static_cast<RegisterId>(ir.registerCount++);
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::LoadConst), zero,
                         addNumber(0.0), static_cast<IrWord>(IrOpcode::Sub),
                         result, zero, source});
      } else if (core == CoreOperator::LogicalNot) {
        const auto source = self(self, operation->capture(0));
        const auto trueConstant = addBoolean(true);
        const auto falseConstant = addBoolean(false);
        const auto branch = ir.words.size();
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(IrOpcode::JumpIfFalse), source, 0,
                         static_cast<IrWord>(IrOpcode::LoadConst), result,
                         falseConstant, static_cast<IrWord>(IrOpcode::Jump),
                         0});
        const auto falseTarget = ir.words.size();
        ir.words.insert(
            ir.words.end(),
            {static_cast<IrWord>(IrOpcode::LoadConst), result, trueConstant});
        const auto endTarget = ir.words.size();
        ir.words[branch + 2] = falseTarget;
        ir.words[branch + 7] = endTarget;
      } else if (core == CoreOperator::LogicalAnd ||
                 core == CoreOperator::LogicalOr) {
        const auto left = self(self, operation->capture(0));
        const auto trueConstant = addBoolean(true);
        const auto falseConstant = addBoolean(false);
        if (core == CoreOperator::LogicalAnd) {
          const auto leftBranch = ir.words.size();
          ir.words.insert(
              ir.words.end(),
              {static_cast<IrWord>(IrOpcode::JumpIfFalse), left, 0});
          const auto right = self(self, operation->capture(1));
          const auto rightBranch = ir.words.size();
          ir.words.insert(ir.words.end(),
                          {static_cast<IrWord>(IrOpcode::JumpIfFalse), right, 0,
                           static_cast<IrWord>(IrOpcode::LoadConst), result,
                           trueConstant, static_cast<IrWord>(IrOpcode::Jump),
                           0});
          const auto falseTarget = ir.words.size();
          ir.words.insert(ir.words.end(),
                          {static_cast<IrWord>(IrOpcode::LoadConst), result,
                           falseConstant});
          const auto endTarget = ir.words.size();
          ir.words[leftBranch + 2] = falseTarget;
          ir.words[rightBranch + 2] = falseTarget;
          ir.words[rightBranch + 7] = endTarget;
        } else {
          const auto leftBranch = ir.words.size();
          ir.words.insert(ir.words.end(),
                          {static_cast<IrWord>(IrOpcode::JumpIfFalse), left, 0,
                           static_cast<IrWord>(IrOpcode::LoadConst), result,
                           trueConstant, static_cast<IrWord>(IrOpcode::Jump),
                           0});
          const auto rightTarget = ir.words.size();
          const auto right = self(self, operation->capture(1));
          const auto rightBranch = ir.words.size();
          ir.words.insert(ir.words.end(),
                          {static_cast<IrWord>(IrOpcode::JumpIfFalse), right, 0,
                           static_cast<IrWord>(IrOpcode::LoadConst), result,
                           trueConstant, static_cast<IrWord>(IrOpcode::Jump),
                           0});
          const auto falseTarget = ir.words.size();
          ir.words.insert(ir.words.end(),
                          {static_cast<IrWord>(IrOpcode::LoadConst), result,
                           falseConstant});
          const auto endTarget = ir.words.size();
          ir.words[leftBranch + 2] = rightTarget;
          ir.words[leftBranch + 7] = endTarget;
          ir.words[rightBranch + 2] = falseTarget;
          ir.words[rightBranch + 7] = endTarget;
        }
      } else {
        const auto left = self(self, operation->capture(0));
        const auto right = self(self, operation->capture(1));
        IrOpcode opcode;
        IrComparison comparison = IrComparison::Equal;
        switch (core) {
        case CoreOperator::Add:
          opcode = IrOpcode::Add;
          break;
        case CoreOperator::Subtract:
          opcode = IrOpcode::Sub;
          break;
        case CoreOperator::Multiply:
          opcode = IrOpcode::Mul;
          break;
        case CoreOperator::Divide:
          opcode = IrOpcode::Div;
          break;
        case CoreOperator::Modulo:
          opcode = IrOpcode::Mod;
          break;
        case CoreOperator::StrictEqual:
          opcode = IrOpcode::Compare;
          break;
        case CoreOperator::StrictNotEqual:
          opcode = IrOpcode::Compare;
          comparison = IrComparison::NotEqual;
          break;
        case CoreOperator::Less:
          opcode = IrOpcode::Compare;
          comparison = IrComparison::Less;
          break;
        case CoreOperator::LessEqual:
          opcode = IrOpcode::Compare;
          comparison = IrComparison::LessEqual;
          break;
        case CoreOperator::Greater:
          opcode = IrOpcode::Compare;
          comparison = IrComparison::Greater;
          break;
        case CoreOperator::GreaterEqual:
          opcode = IrOpcode::Compare;
          comparison = IrComparison::GreaterEqual;
          break;
        default:
          throw IntegerParserError("expression has no direct IR lowering yet");
        }
        ir.words.insert(ir.words.end(),
                        {static_cast<IrWord>(opcode), result, left, right});
        if (opcode == IrOpcode::Compare)
          ir.words.push_back(static_cast<IrWord>(comparison));
      }
    } else {
      throw IntegerParserError("expression has no direct IR lowering yet");
    }
    const auto &span = value->sourceSpan;
    ir.sourceMap.push_back(IrSourceMapEntry{
        emittedAt,
        {span.startLine, span.startColumn, span.endLine, span.endColumn}});
    return result;
  };
  const auto result = lower(lower, expression);
  ir.words.insert(ir.words.end(),
                  {static_cast<IrWord>(IrOpcode::Return), result, 0,
                   static_cast<IrWord>(IrOpcode::End)});
#ifndef NDEBUG
  if (codegenTraceEnabled()) {
    std::clog << "[felidae.codegen] action=lower_expression verified_ir_words="
              << ir.words.size() << " registers=" << ir.registerCount
              << " constants=" << ir.constants.size()
              << " symbols=" << ir.symbols.size() << '\n';
  }
#endif
  return ir;
}

FelidaeIr IrCodeGenerator::lowerGlobalBinding(
    const GlobalBindingStmt &binding,
    const std::unordered_set<SymbolId> &factTypes) {
  if (!binding.expr)
    throw IntegerParserError("Global binding has no value expression");
  auto ir = lowerExpression(binding.expr, factTypes);
  if (ir.words.size() < 4 ||
      ir.words[ir.words.size() - 4] != static_cast<IrWord>(IrOpcode::Return) ||
      ir.words.back() != static_cast<IrWord>(IrOpcode::End)) {
    throw IntegerParserError("Expression IR is missing its terminal return");
  }
  const auto result = ir.words[ir.words.size() - 3];
  ir.symbols.push_back(symbolIdForName(binding.name));
  const auto symbol = static_cast<IrWord>(ir.symbols.size() - 1);
  ir.words.resize(ir.words.size() - 4);
  ir.words.insert(ir.words.end(),
                  {
                      static_cast<IrWord>(IrOpcode::StoreSymbol),
                      symbol,
                      result,
                      static_cast<IrWord>(IrOpcode::LoadSymbol),
                      result,
                      symbol,
                      static_cast<IrWord>(IrOpcode::Return),
                      result,
                      0,
                      static_cast<IrWord>(IrOpcode::End),
                  });
  const auto &span = binding.sourceSpan;
  ir.sourceMap.push_back(IrSourceMapEntry{
      ir.words.size() - 10,
      {span.startLine, span.startColumn, span.endLine, span.endColumn}});
  return ir;
}

FelidaeIr IrCodeGenerator::lowerEntryMethod(
    const ClauseStmt &method, const std::unordered_set<SymbolId> &factTypes) {
  if (method.body.size() != 1 || !method.fallbackBranches.empty()) {
    throw IntegerParserError(
        "Method has not reached direct entry IR lowering yet");
  }
  const auto returned =
      std::dynamic_pointer_cast<ReturnGoal>(method.body.front());
  if (!returned)
    throw IntegerParserError("Direct entry IR lowering requires a return goal");
  if (returned->fields.empty()) {
    auto nil = std::make_shared<NilExpr>();
    nil->sourceSpan = returned->sourceSpan;
    return lowerExpression(nil, factTypes);
  }
  bool named = false;
  for (const auto &field : returned->fields)
    named = named || !field.name.empty();
  if (!named) {
    if (returned->fields.size() != 1) {
      throw IntegerParserError(
          "Direct entry IR lowering requires one positional return value");
    }
    return lowerExpression(returned->fields.front().value, factTypes);
  }
  std::vector<MapEntry> entries;
  entries.reserve(returned->fields.size());
  for (const auto &field : returned->fields) {
    if (field.name.empty()) {
      throw IntegerParserError("Direct entry IR lowering cannot mix named and "
                               "positional return fields");
    }
    entries.emplace_back(field.name, field.nameId, field.value);
  }
  auto map = std::make_shared<MapExpr>(std::move(entries));
  map->sourceSpan = returned->sourceSpan;
  return lowerExpression(map, factTypes);
}


IrModule IrCodeGenerator::compile(Program program) const {
  desugarWhereGuardedClauses(program);
  normalizeFactQueries(program);
  IrModule module;
  // The strict compiler accepts only constructs that lower to verified IR.
  // Unsupported constructs are rejected with a source span; there is no
  // interpreter or compatibility execution path.
  std::unordered_set<SymbolId> directSymbols;
  std::unordered_set<SymbolId> procedureSymbols;
  std::unordered_set<SymbolId> factTypes;
  for (const auto &clause : program.clauses) {
    if (!clause)
      continue;
    const bool conflict =
        clause->isFact()
            ? procedureSymbols.contains(clause->head.nameId)
            : factTypes.contains(clause->head.nameId) ||
                  !procedureSymbols.insert(clause->head.nameId).second;
    if (conflict) {
      throw IntegerParserError(
          "not yet lowered to IR: duplicate procedure declaration at " +
          std::to_string(clause->sourceSpan.startLine) + ":" +
          std::to_string(clause->sourceSpan.startColumn));
    }
    // A fact predicate is a table and therefore may have any number of
    // source rows. Only callable procedures are single declarations.
    if (clause->isFact())
      factTypes.insert(clause->head.nameId);
  }
  for (const auto &clause : program.clauses) {
    if (!clause)
      continue;
    for (const auto &annotation : clause->annotations) {
      const bool operatorAnnotation =
          annotation.builtinId == BuiltinId::OverloadAnnotation ||
          annotation.builtinId == BuiltinId::MixfixAnnotation ||
          annotation.builtinId == BuiltinId::MatcherAnnotation;
      if (!operatorAnnotation &&
          !procedureSymbols.contains(annotation.nameId)) {
        throw IntegerParserError(
            "unknown annotation '" + annotation.name + "' at " +
            std::to_string(annotation.sourceSpan.startLine) + ":" +
            std::to_string(annotation.sourceSpan.startColumn));
      }
    }
  }
  // Module bindings form one immutable namespace.  Reject collisions before
  // IR generation so a valid-looking FELBIR cannot defer a scope error until VM
  // execution.  Procedure locals are checked separately against parameters
  // and globals by isDirectProcedure/isDirectGoalSequence.
  std::unordered_set<SymbolId> globalSymbols;
  for (const auto &binding : program.globals) {
    if (!binding || binding->name.empty()) {
      throw IntegerParserError(
          "not yet lowered to IR: invalid global binding at " +
          std::to_string(binding ? binding->sourceSpan.startLine : 1) + ":" +
          std::to_string(binding ? binding->sourceSpan.startColumn : 1));
    }
    const auto symbol = symbolIdForName(binding->name);
    if (!globalSymbols.insert(symbol).second ||
        procedureSymbols.contains(symbol) || factTypes.contains(symbol)) {
      throw IntegerParserError(
          "not yet lowered to IR: duplicate or conflicting global binding at " +
          std::to_string(binding->sourceSpan.startLine) + ":" +
          std::to_string(binding->sourceSpan.startColumn));
    }
  }
  // Resolve fields after all module names and method parameters are known.
  // This is compiler-local AST normalization; Form receives only the
  // resulting GetField IR and retains no syntax dependency.
  std::unordered_set<SymbolId> lexicalGlobals;
  for (auto &binding : program.globals) {
    if (!binding)
      continue;
    binding->expr = resolveScopedAccess(binding->expr, lexicalGlobals);
    lexicalGlobals.insert(symbolIdForName(binding->name));
  }
  for (auto &clause : program.clauses) {
    if (!clause || clause->isFact())
      continue;
    auto visible = globalSymbols;
    for (const auto parameter : procedureParameters(*clause))
      visible.insert(parameter);
    resolveScopedAccessesInGoals(clause->body, std::move(visible));
  }
  std::unordered_map<IrSymbolRef, std::size_t> factTypeIndexes;
  for (const auto &clause : program.clauses) {
    if (!clause || !clause->isFact())
      continue;
    std::vector<IrSymbolRef> parents;
    for (const auto &parent : clause->parentNames) {
      const auto parentSymbol = symbolIdForName(parent);
      if (!factTypes.contains(parentSymbol)) {
        throw IntegerParserError(
            "not yet lowered to IR: unknown fact parent at " +
            std::to_string(clause->sourceSpan.startLine) + ":" +
            std::to_string(clause->sourceSpan.startColumn));
      }
      parents.push_back(parentSymbol);
    }
    const auto existing = factTypeIndexes.find(clause->head.nameId);
    if (existing == factTypeIndexes.end()) {
      factTypeIndexes.emplace(clause->head.nameId, module.factTypes.size());
      module.factTypes.push_back(
          {clause->head.nameId,
           std::move(parents),
           {clause->sourceSpan.startLine, clause->sourceSpan.startColumn,
            clause->sourceSpan.endLine, clause->sourceSpan.endColumn}});
    } else if (module.factTypes[existing->second].parents != parents) {
      throw IntegerParserError(
          "not yet lowered to IR: inconsistent fact hierarchy at " +
          std::to_string(clause->sourceSpan.startLine) + ":" +
          std::to_string(clause->sourceSpan.startColumn));
    }
  }
  const DirectCompileContext context{procedureSymbols, factTypes};
  // Builtin modules are compiler-known operation namespaces, not source or
  // dynamic-library dependencies. Their imports therefore need no secondary
  // parser/execution path. Other imports remain explicitly unsupported until
  // they have a verified IR module-linking contract.
  //
  // This list previously omitted array/math/str/file/db/system even though
  // every one of their declared calls (array.get, math:sqrt, str:upper,
  // file.readFile, system.print, ...) already compiles and runs
  // fine with no import statement at all -- an explicit `import "array"`
  // (etc.) was the only thing that broke the file, rejecting an otherwise
  // valid program. Names still absent here (process, http, gtk, qt,
  // wordnet, plot, fact, fact_analysis, flibrary) route through the
  // system_library_loader bridge, which has no implementation anywhere;
  // allowlisting their import would let a file compile while its calls
  // silently return nil, which is worse than the current explicit rejection.
  const bool builtinImports = std::all_of(
      program.imports.begin(), program.imports.end(), [](const auto &import) {
        return import && std::all_of(import->paths.begin(), import->paths.end(),
                                     [](const std::string &path) {
                                       return path == "json" || path == "csv" ||
                                              path == "group" ||
                                              path == "set" || path == "ml" ||
                                              path == "array" ||
                                              path == "math" ||
                                              path == "str" ||
                                              path == "file" ||
                                              path == "db" ||
                                              path == "system";
                                     });
      });
  bool directGlobals = builtinImports && !program.clauses.empty();
  for (const auto &binding : program.globals) {
    if (!binding || !isDirectDeterministicExpression(binding->expr,
                                                     directSymbols, context)) {
      directGlobals = false;
      break;
    }
    directSymbols.insert(symbolIdForName(binding->name));
  }
  const auto main = std::find_if(
      program.clauses.begin(), program.clauses.end(), [](const auto &clause) {
        return clause && !clause->isFact() &&
               clause->head.nameId == symbolIdForName("main");
      });
  const bool eligibleMethods = std::all_of(
      program.clauses.begin(), program.clauses.end(), [&](const auto &clause) {
        return clause &&
               (clause->isFact() ||
                (clause->head.nameId == symbolIdForName("main")
                     ? isDirectEntry(*clause, directSymbols, context)
                     : isDirectProcedure(*clause, directSymbols, context)));
      });
  if (directGlobals && main != program.clauses.end() && eligibleMethods &&
      isDirectEntry(**main, directSymbols, context)) {
    // Globals must execute before fact rows: a fact field may read a global
    // by name (e.g. Person(age: defaultAge)), which lowers to a LoadSymbol
    // that requires the matching StoreSymbol to have already run. Global
    // bindings never read a fact by name (facts are reached by type/query,
    // not symbol lookup), so this order has no matching requirement the
    // other way.
    for (const auto &binding : program.globals) {
      appendFragment(module.ir,
                     IrCodeGenerator::lowerGlobalBinding(*binding, factTypes),
                     true);
    }
    for (const auto &clause : program.clauses) {
      if (!clause || !clause->isFact() || clause->emptyDeclaration)
        continue;
      auto fact = std::make_shared<TermExpr>(
          clause->head.name, clause->head.nameId, clause->head.args,
          clause->head.builtinId, true);
      fact->sourceSpan = clause->sourceSpan;
      if (!isDirectDeterministicExpression(fact, directSymbols, context)) {
        throw IntegerParserError(
            "not yet lowered to IR: non-deterministic fact row at " +
            std::to_string(clause->sourceSpan.startLine) + ":" +
            std::to_string(clause->sourceSpan.startColumn));
      }
      appendFragment(
          module.ir,
          IrCodeGenerator::lowerExpression(std::move(fact), factTypes), true);
    }
    for (const auto &clause : program.clauses) {
      if (clause->isFact())
        continue;
      const auto parameters = procedureParameters(*clause);
      std::vector<IrSymbolRef> irParameters;
      irParameters.reserve(parameters.size());
      for (const auto parameter : parameters)
        irParameters.push_back(parameter);
      const auto [_, inserted] = module.procedures.emplace(
          clause->head.nameId,
          IrProcedure{
              compileDirectProcedure(*clause, factTypes),
              irParameters,
              irParameters,
              {clause->sourceSpan.startLine, clause->sourceSpan.startColumn,
               clause->sourceSpan.endLine, clause->sourceSpan.endColumn}});
      if (!inserted)
        throw IntegerParserError("duplicate direct IR procedure");
    }
    const auto entryRegister = static_cast<IrWord>(module.ir.registerCount++);
    module.entryProcedure = (*main)->head.nameId;
    module.ir.symbols.push_back((*main)->head.nameId);
    const auto entrySymbol = static_cast<IrWord>(module.ir.symbols.size() - 1);
    module.ir.words.insert(module.ir.words.end(),
                           {
                               static_cast<IrWord>(IrOpcode::Call),
                               entryRegister,
                               entrySymbol,
                               0,
                               static_cast<IrWord>(IrOpcode::Return),
                               entryRegister,
                               0,
                               static_cast<IrWord>(IrOpcode::End),
                           });
    const auto &span = (*main)->sourceSpan;
    module.ir.sourceMap.push_back(IrSourceMapEntry{
        module.ir.words.size() - 8,
        {span.startLine, span.startColumn, span.endLine, span.endColumn}});
    return module;
  }
  SourceSpan span;
  bool foundUnsupportedSpan = false;
  for (const auto &binding : program.globals) {
    if (!binding || !isDirectDeterministicExpression(binding->expr,
                                                     directSymbols, context)) {
      span = binding && binding->expr
                 ? firstUnsupportedExpression(binding->expr, directSymbols,
                                              context)
                 : SourceSpan{};
      foundUnsupportedSpan = true;
      break;
    }
  }
  if (!foundUnsupportedSpan) {
    for (const auto &clause : program.clauses) {
      const auto eligible =
          clause &&
          (clause->isFact() ||
           (clause->head.nameId == symbolIdForName("main")
                ? isDirectEntry(*clause, directSymbols, context)
                : isDirectProcedure(*clause, directSymbols, context)));
      if (!eligible) {
        span = clause
                   ? firstUnsupportedProcedure(*clause, directSymbols, context)
                   : SourceSpan{};
        foundUnsupportedSpan = true;
        break;
      }
    }
  }
  if (!foundUnsupportedSpan && !program.statements.empty() &&
      program.statements.front()) {
    span = program.statements.front()->sourceSpan;
  }
  throw IntegerParserError(
      "not yet lowered to IR: unsupported module construct at " +
      std::to_string(span.startLine) + ":" + std::to_string(span.startColumn));
}

} // namespace Felidae
