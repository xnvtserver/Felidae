#include "Interpreter.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace Felidae {

namespace {

std::shared_ptr<MapExpr> markFact(
    std::shared_ptr<MapExpr> value,
    std::string type) {
    value->factType = std::move(type);
    return value;
}

} // namespace

bool Interpreter::isTableEligibleGoal(
    const std::shared_ptr<Goal>& goal,
    SymbolId root,
    std::unordered_set<SymbolId>& visiting,
    std::unordered_set<SymbolId>& checked,
    bool& recursive) const {
    if (!goal) return false;
    switch (goal->kind()) {
        case GoalKind::Call: {
            const auto call = std::static_pointer_cast<CallGoal>(goal);
            if (call->call.builtinId != BuiltinId::Unknown ||
                nativeDeclarationFor(call->call.name)) return false;
            const SymbolId id = call->call.nameId == 0
                ? symbolIdForName(call->call.name)
                : call->call.nameId;
            if (id == root || visiting.count(id)) recursive = true;
            return isTableEligiblePredicate(
                call->call.name, id, visiting, checked, recursive);
        }
        case GoalKind::Not: {
            const auto negated = std::static_pointer_cast<NotGoal>(goal);
            if (negated->call.builtinId != BuiltinId::Unknown ||
                nativeDeclarationFor(negated->call.name)) return false;
            const SymbolId id = negated->call.nameId == 0
                ? symbolIdForName(negated->call.name)
                : negated->call.nameId;
            return isTableEligiblePredicate(
                negated->call.name, id, visiting, checked, recursive);
        }
        case GoalKind::Binary:
            return true;
        case GoalKind::Where:
            return isTableEligibleGoal(
                std::static_pointer_cast<WhereGoal>(goal)->condition,
                root,
                visiting,
                checked,
                recursive);
        case GoalKind::Assign:
            return !exprMayHaveSideEffects(
                std::static_pointer_cast<AssignGoal>(goal)->expr);
        case GoalKind::Return:
            return std::static_pointer_cast<ReturnGoal>(goal)->fields.empty();
        // Branching and value returns retain ordered method semantics.
        // Tabled rules are intentionally declarative conjunctions.
        case GoalKind::MultiAssign:
        case GoalKind::If:
        case GoalKind::Group:
        case GoalKind::Or:
            return false;
    }
    return false;
}

bool Interpreter::isTableEligiblePredicate(
    const std::string& name,
    SymbolId nameId,
    std::unordered_set<SymbolId>& visiting,
    std::unordered_set<SymbolId>& checked,
    bool& recursive) const {
    if (checked.count(nameId)) return true;
    if (visiting.count(nameId)) {
        recursive = true;
        return true;
    }
    visiting.insert(nameId);
    const auto* clauses = findClauses(name, nameId);
    if (clauses) {
        for (const auto& clause : *clauses) {
            if (!clause || clause->clauseKind != ClauseKind::Rule ||
                isMethodClause(*clause) ||
                !clause->fallbackBranches.empty()) {
                visiting.erase(nameId);
                return false;
            }
            for (const auto& goal : clause->body) {
                if (!isTableEligibleGoal(
                        goal, nameId, visiting, checked, recursive)) {
                    visiting.erase(nameId);
                    return false;
                }
            }
        }
    }
    visiting.erase(nameId);
    checked.insert(nameId);
    return true;
}

bool Interpreter::tableEvaluationValid(
    const TableEvaluation& evaluation) const {
    if (evaluation.hierarchyGeneration != memory_.hierarchyGeneration()) {
        return false;
    }
    for (const auto& dependency : evaluation.callableGenerations) {
        const auto current = symbolGenerations_.find(dependency.first);
        const std::uint64_t generation =
            current == symbolGenerations_.end() ? 0 : current->second;
        if (generation != dependency.second) return false;
    }
    for (const auto& dependency : evaluation.relationGenerations) {
        const std::string type = symbolNameForId(dependency.first);
        if (memory_.relationGeneration(type, dependency.first) !=
            dependency.second) return false;
    }
    return true;
}

std::shared_ptr<Interpreter::TableEvaluation>
Interpreter::tableEvaluationFor(
    const std::string& name,
    SymbolId nameId,
    bool requireRecursive) {
    if (nameId == 0) nameId = symbolIdForName(name);
    const auto cached = tableCache_.find(nameId);
    if (cached != tableCache_.end() && cached->second &&
        cached->second->rootName == name &&
        tableEvaluationValid(*cached->second)) {
        ++tableCacheHits_;
        return cached->second;
    }
    if (cached != tableCache_.end()) tableCache_.erase(cached);

    std::unordered_set<SymbolId> visiting;
    std::unordered_set<SymbolId> checked;
    bool recursive = false;
    if (!isTableEligiblePredicate(
            name, nameId, visiting, checked, recursive) ||
        (requireRecursive && !recursive)) {
        return {};
    }

    ++tableCacheMisses_;
    auto evaluation = buildTableEvaluation(name, nameId);
    if (evaluation) tableCache_[nameId] = evaluation;
    return evaluation;
}

std::vector<Interpreter::TableBinding> Interpreter::evaluateTableGoals(
    const std::vector<std::shared_ptr<Goal>>& goals,
    std::vector<TableBinding> inputs,
    const TableEvaluation& evaluation,
    std::optional<std::pair<SymbolId, std::size_t>> deltaPivot) {
    constexpr std::size_t MaxIntermediateBindings = 1'000'000;
    for (std::size_t goalIndex = 0;
         goalIndex < goals.size() && !inputs.empty();
         ++goalIndex) {
        const auto& goal = goals[goalIndex];
        if (auto call = std::dynamic_pointer_cast<CallGoal>(goal)) {
            const SymbolId id = call->call.nameId == 0
                ? symbolIdForName(call->call.name)
                : call->call.nameId;
            const auto table = evaluation.predicates.find(id);
            if (table == evaluation.predicates.end()) return {};

            std::vector<std::size_t> allCandidates;
            const std::vector<std::size_t>* candidates = nullptr;
            if (deltaPivot && deltaPivot->first == id &&
                deltaPivot->second == goalIndex) {
                candidates = &table->second.delta;
            } else {
                allCandidates.resize(table->second.answers.size());
                for (std::size_t i = 0; i < allCandidates.size(); ++i) {
                    allCandidates[i] = i;
                }
                candidates = &allCandidates;
            }

            std::vector<TableBinding> next;
            for (const auto& input : inputs) {
                for (const std::size_t answerIndex : *candidates) {
                    if (answerIndex >= table->second.answers.size()) continue;
                    const auto& answer = table->second.answers[answerIndex];
                    for (auto& unified :
                         unifyCallAlternatives(
                             call->call, answer.call, input.env)) {
                        TableBinding binding{
                            std::move(unified), input.provenance};
                        binding.provenance.insert(
                            binding.provenance.end(),
                            answer.provenance.begin(),
                            answer.provenance.end());
                        next.push_back(std::move(binding));
                        if (next.size() > MaxIntermediateBindings) {
                            throw InterpreterError(
                                "TableLimit: intermediate binding limit reached");
                        }
                    }
                }
            }
            inputs = std::move(next);
            continue;
        }

        if (auto negated = std::dynamic_pointer_cast<NotGoal>(goal)) {
            const SymbolId id = negated->call.nameId == 0
                ? symbolIdForName(negated->call.name)
                : negated->call.nameId;
            const auto table = evaluation.predicates.find(id);
            std::vector<TableBinding> next;
            for (auto& input : inputs) {
                bool matched = false;
                if (table != evaluation.predicates.end()) {
                    for (const auto& answer : table->second.answers) {
                        if (!unifyCallAlternatives(
                                negated->call, answer.call, input.env).empty()) {
                            matched = true;
                            break;
                        }
                    }
                }
                if (!matched) next.push_back(std::move(input));
            }
            inputs = std::move(next);
            continue;
        }

        std::vector<TableBinding> next;
        for (auto& input : inputs) {
            bool accepted = false;
            if (auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
                accepted = solveBinaryGoal(*binary, input.env);
            } else if (auto where =
                           std::dynamic_pointer_cast<WhereGoal>(goal)) {
                accepted = solveWhereGoal(*where, input.env);
            } else if (auto assign =
                           std::dynamic_pointer_cast<AssignGoal>(goal)) {
                accepted = solveAssignGoal(*assign, input.env);
            } else if (auto returned =
                           std::dynamic_pointer_cast<ReturnGoal>(goal)) {
                accepted = returned->fields.empty();
            }
            if (accepted) next.push_back(std::move(input));
        }
        inputs = std::move(next);
    }
    return inputs;
}

std::shared_ptr<Interpreter::TableEvaluation>
Interpreter::buildTableEvaluation(
    const std::string& name,
    SymbolId nameId) {
    constexpr std::size_t MaxTableAnswers = 1'000'000;
    constexpr std::size_t MaxProvenanceNodes = 2'000'000;
    constexpr std::size_t MaxSemiNaiveRounds = 100'000;
    constexpr std::size_t MaxProofsPerAnswer = 16;

    struct RuleInfo {
        std::shared_ptr<ClauseStmt> clause;
        SymbolId headId = 0;
        std::vector<std::pair<SymbolId, bool>> dependencies;
    };

    auto evaluation = std::make_shared<TableEvaluation>();
    evaluation->rootId = nameId;
    evaluation->rootName = name;
    evaluation->hierarchyGeneration = memory_.hierarchyGeneration();

    std::unordered_map<SymbolId, std::string> names;
    std::vector<RuleInfo> rules;
    std::vector<SymbolId> pending{nameId};
    names.emplace(nameId, name);
    std::unordered_set<SymbolId> visited;
    while (!pending.empty()) {
        const SymbolId currentId = pending.back();
        pending.pop_back();
        if (!visited.insert(currentId).second) continue;
        const std::string currentName = names[currentId];
        const auto* clauses = findClauses(currentName, currentId);
        if (!clauses) continue;
        for (const auto& clause : *clauses) {
            if (!clause || clause->clauseKind != ClauseKind::Rule) continue;
            RuleInfo rule{clause, currentId, {}};
            for (const auto& goal : clause->body) {
                if (auto call =
                        std::dynamic_pointer_cast<CallGoal>(goal)) {
                    const SymbolId target = call->call.nameId == 0
                        ? symbolIdForName(call->call.name)
                        : call->call.nameId;
                    rule.dependencies.emplace_back(target, false);
                    names.emplace(target, call->call.name);
                    pending.push_back(target);
                } else if (auto negated =
                               std::dynamic_pointer_cast<NotGoal>(goal)) {
                    const SymbolId target = negated->call.nameId == 0
                        ? symbolIdForName(negated->call.name)
                        : negated->call.nameId;
                    rule.dependencies.emplace_back(target, true);
                    names.emplace(target, negated->call.name);
                    pending.push_back(target);
                }
            }
            rules.push_back(std::move(rule));
        }
    }

    for (const auto& entry : names) {
        const auto generation = symbolGenerations_.find(entry.first);
        evaluation->callableGenerations[entry.first] =
            generation == symbolGenerations_.end() ? 0 : generation->second;
    }

    for (const auto& entry : names) {
        PredicateTable table;
        table.name = entry.second;
        table.nameId = entry.first;
        auto inserted =
            evaluation->predicates.emplace(entry.first, std::move(table));
        auto& predicate = inserted.first->second;
        for (const std::size_t factIndex :
             memory_.compatibleFactIndexes(entry.second, entry.first)) {
            const auto& fact = memory_.fact(factIndex);
            if (!fact.active) continue;
            Call answer(entry.second, {});
            answer.nameId = entry.first;
            answer.args = memory_.factArguments(factIndex);
            const std::string key = answer.debug();

            ProvenanceNode node;
            node.kind = ProvenanceNode::Kind::Fact;
            node.factId = fact.id;
            const std::size_t provenance = evaluation->provenance.size();
            evaluation->provenance.push_back(std::move(node));

            const auto existing = predicate.answerByKey.find(key);
            if (existing == predicate.answerByKey.end()) {
                const std::size_t answerIndex = predicate.answers.size();
                predicate.answerByKey.emplace(key, answerIndex);
                predicate.answers.push_back(
                    TableAnswer{std::move(answer), {provenance}});
                predicate.delta.push_back(answerIndex);
            } else if (
                predicate.answers[existing->second].provenance.size() <
                MaxProofsPerAnswer) {
                predicate.answers[existing->second].provenance.push_back(
                    provenance);
            }
            evaluation->relationGenerations[fact.typeId] =
                memory_.relationGeneration(fact.type, fact.typeId);
        }
        evaluation->relationGenerations.try_emplace(
            entry.first,
            memory_.relationGeneration(entry.second, entry.first));
    }

    std::unordered_map<SymbolId, std::size_t> stratum;
    for (const auto& entry : names) stratum.emplace(entry.first, 0);
    bool changed = false;
    for (std::size_t pass = 0; pass <= names.size(); ++pass) {
        changed = false;
        for (const auto& rule : rules) {
            for (const auto& dependency : rule.dependencies) {
                const std::size_t required =
                    stratum[dependency.first] +
                    (dependency.second ? 1U : 0U);
                if (stratum[rule.headId] < required) {
                    stratum[rule.headId] = required;
                    changed = true;
                }
            }
        }
        if (!changed) break;
        if (pass == names.size()) {
            throw InterpreterError(
                "Unstratified negative dependency cycle in tabled reasoning");
        }
    }

    std::size_t maxStratum = 0;
    for (const auto& entry : stratum) {
        maxStratum = std::max(maxStratum, entry.second);
    }

    for (std::size_t currentStratum = 0;
         currentStratum <= maxStratum;
         ++currentStratum) {
        std::vector<const RuleInfo*> stratumRules;
        std::unordered_set<SymbolId> activeHeads;
        for (const auto& rule : rules) {
            if (stratum[rule.headId] == currentStratum) {
                stratumRules.push_back(&rule);
                activeHeads.insert(rule.headId);
            }
        }
        if (stratumRules.empty()) continue;

        bool firstRound = true;
        for (;;) {
            if (++evaluation->rounds > MaxSemiNaiveRounds) {
                throw InterpreterError(
                    "TableLimit: semi-naive iteration limit reached");
            }
            struct PendingAnswer {
                SymbolId headId = 0;
                Call call;
                std::size_t provenance = 0;
            };
            std::vector<PendingAnswer> pendingAnswers;

            for (const RuleInfo* rule : stratumRules) {
                std::vector<std::pair<SymbolId, std::size_t>> pivots;
                for (std::size_t goalIndex = 0;
                     goalIndex < rule->clause->body.size();
                     ++goalIndex) {
                    const auto call = std::dynamic_pointer_cast<CallGoal>(
                        rule->clause->body[goalIndex]);
                    if (!call) continue;
                    const SymbolId target = call->call.nameId == 0
                        ? symbolIdForName(call->call.name)
                        : call->call.nameId;
                    if (stratum[target] == currentStratum) {
                        const auto table =
                            evaluation->predicates.find(target);
                        if (table != evaluation->predicates.end() &&
                            !table->second.delta.empty()) {
                            pivots.emplace_back(target, goalIndex);
                        }
                    }
                }
                if (pivots.empty() && !firstRound) continue;
                if (pivots.empty()) pivots.emplace_back(0, 0);

                for (const auto& pivot : pivots) {
                    std::vector<TableBinding> bindings{
                        TableBinding{Env{}, {}}};
                    bindings = evaluateTableGoals(
                        rule->clause->body,
                        std::move(bindings),
                        *evaluation,
                        pivot.first == 0
                            ? std::nullopt
                            : std::optional<
                                  std::pair<SymbolId, std::size_t>>(pivot));
                    for (auto& binding : bindings) {
                        Call answer(rule->clause->head.name, {});
                        answer.nameId = rule->headId;
                        bool ground = true;
                        for (const auto& argument :
                             rule->clause->head.args) {
                            const auto resolved =
                                resolveExpr(argument.value, binding.env);
                            if (!isGroundLiteral(resolved)) {
                                ground = false;
                                break;
                            }
                            answer.args.push_back(
                                Arg{argument.name, argument.nameId, resolved->clone()});
                        }
                        if (!ground) continue;
                        if (evaluation->provenance.size() >=
                            MaxProvenanceNodes) {
                            throw InterpreterError(
                                "DerivationLimit: provenance node limit reached");
                        }
                        ProvenanceNode node;
                        node.kind = ProvenanceNode::Kind::Rule;
                        node.rule = rule->clause->head.name;
                        node.span = rule->clause->sourceSpan;
                        node.parents = std::move(binding.provenance);
                        const std::size_t provenance =
                            evaluation->provenance.size();
                        evaluation->provenance.push_back(std::move(node));
                        pendingAnswers.push_back(PendingAnswer{
                            rule->headId,
                            std::move(answer),
                            provenance});
                    }
                }
            }

            for (const SymbolId head : activeHeads) {
                evaluation->predicates[head].delta.clear();
            }
            bool progress = false;
            for (auto& pendingAnswer : pendingAnswers) {
                auto& table =
                    evaluation->predicates[pendingAnswer.headId];
                const std::string key = pendingAnswer.call.debug();
                const auto existing = table.answerByKey.find(key);
                if (existing != table.answerByKey.end()) {
                    auto& proofs =
                        table.answers[existing->second].provenance;
                    if (proofs.size() < MaxProofsPerAnswer) {
                        proofs.push_back(pendingAnswer.provenance);
                    }
                    continue;
                }
                if (table.answers.size() >= MaxTableAnswers) {
                    throw InterpreterError(
                        "TableLimit: answer limit reached for predicate '" +
                        table.name + "'");
                }
                const std::size_t answerIndex = table.answers.size();
                table.answerByKey.emplace(key, answerIndex);
                table.answers.push_back(TableAnswer{
                    std::move(pendingAnswer.call),
                    {pendingAnswer.provenance}});
                table.delta.push_back(answerIndex);
                ++evaluation->deltaAnswers;
                progress = true;
            }
            firstRound = false;
            if (!progress) break;
        }
    }

    tableRounds_ += evaluation->rounds;
    tableDeltaAnswers_ += evaluation->deltaAnswers;
    provenanceNodes_ += evaluation->provenance.size();
    return evaluation;
}

bool Interpreter::tableCallAnswers(
    const Call& call,
    const Env& env,
    std::vector<TableBinding>& bindings,
    std::shared_ptr<TableEvaluation>* evaluationOut,
    bool requireRecursive) {
    const SymbolId id =
        call.nameId == 0 ? symbolIdForName(call.name) : call.nameId;
    auto evaluation =
        tableEvaluationFor(call.name, id, requireRecursive);
    if (!evaluation) return false;
    if (evaluationOut) *evaluationOut = evaluation;
    const auto table = evaluation->predicates.find(id);
    if (table == evaluation->predicates.end()) return true;
    for (const auto& answer : table->second.answers) {
        for (auto& unified :
             unifyCallAlternatives(call, answer.call, env)) {
            bindings.push_back(
                TableBinding{std::move(unified), answer.provenance});
        }
    }
    return true;
}

namespace {

std::shared_ptr<Expr> reasoningMapValue(
    const std::shared_ptr<Expr>& value,
    const std::string& key) {
    const auto map = std::dynamic_pointer_cast<MapExpr>(value);
    if (!map) return {};
    const SymbolId keyId = symbolIdForName(key);
    for (const auto& entry : map->entries) {
        if (entry.keyId == keyId && entry.key == key) return entry.value;
    }
    return {};
}

void reasoningSetValue(std::vector<MapEntry>& entries,
                       const std::string& key,
                       std::shared_ptr<Expr> value) {
    const SymbolId keyId = symbolIdForName(key);
    for (auto& entry : entries) {
        if (entry.keyId == keyId && entry.key == key) {
            entry.value = std::move(value);
            return;
        }
    }
    entries.emplace_back(key, std::move(value));
}

std::shared_ptr<MapExpr> callAsFact(const Call& call) {
    std::vector<MapEntry> entries;
    entries.emplace_back(
        internalSymbolString(InternalSymbolKind::Type),
        std::make_shared<StringExpr>(call.name));
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        const auto& argument = call.args[i];
        entries.emplace_back(
            argument.name.empty()
                ? "value" + std::to_string(i)
                : argument.name,
            argument.value ? argument.value->clone()
                           : std::make_shared<NilExpr>());
    }
    return markFact(
        std::make_shared<MapExpr>(std::move(entries)), call.name);
}

std::shared_ptr<ArrayExpr> stringArray(
    const std::set<std::string>& values) {
    std::vector<std::shared_ptr<Expr>> items;
    items.reserve(values.size());
    for (const auto& value : values) {
        items.push_back(std::make_shared<StringExpr>(value));
    }
    return std::make_shared<ArrayExpr>(std::move(items));
}

std::shared_ptr<ArrayExpr> factIdArray(
    const std::set<std::uint64_t>& values) {
    std::vector<std::shared_ptr<Expr>> items;
    items.reserve(values.size());
    for (const std::uint64_t value : values) {
        items.push_back(markFact(std::make_shared<MapExpr>(
            std::vector<MapEntry>{
                {"__type", std::make_shared<StringExpr>("FactReference")},
                {"fact_id", std::make_shared<StringExpr>(
                    std::to_string(value))}}), "FactReference"));
    }
    return std::make_shared<ArrayExpr>(std::move(items));
}

bool finiteUnitInterval(double value) {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

} // namespace

std::shared_ptr<MapExpr> Interpreter::materializeDerivationResult(
    const Call& query,
    const std::vector<TableBinding>& supporting,
    const std::vector<TableBinding>& opposing,
    const std::shared_ptr<TableEvaluation>& positiveEvaluation,
    const std::shared_ptr<TableEvaluation>& negativeEvaluation) const {
    struct Summary {
        std::set<std::uint64_t> factIds;
        std::set<std::string> rules;
        std::vector<std::shared_ptr<Expr>> explanation;
    };
    const auto summarize = [](const std::vector<TableBinding>& bindings,
                              const std::shared_ptr<TableEvaluation>& evaluation) {
        Summary result;
        if (!evaluation) return result;
        std::unordered_set<std::size_t> visited;
        std::unordered_set<std::uint64_t> explainedFacts;
        std::set<std::tuple<std::string, int, int, int, int>>
            explainedRules;
        std::vector<std::size_t> pending;
        for (const auto& binding : bindings) {
            pending.insert(
                pending.end(),
                binding.provenance.begin(),
                binding.provenance.end());
        }
        while (!pending.empty()) {
            const std::size_t handle = pending.back();
            pending.pop_back();
            if (!visited.insert(handle).second ||
                handle >= evaluation->provenance.size()) continue;
            const auto& node = evaluation->provenance[handle];
            if (node.kind == ProvenanceNode::Kind::Fact) {
                result.factIds.insert(node.factId);
                if (explainedFacts.insert(node.factId).second) {
                    result.explanation.push_back(markFact(std::make_shared<MapExpr>(
                    std::vector<MapEntry>{
                        {"__type", std::make_shared<StringExpr>(
                            "FactProvenance")},
                        {"fact_id", std::make_shared<StringExpr>(
                            std::to_string(node.factId))}}), "FactProvenance"));
                }
            } else {
                result.rules.insert(node.rule);
                const auto identity = std::make_tuple(
                    node.rule,
                    node.span.startLine,
                    node.span.startColumn,
                    node.span.endLine,
                    node.span.endColumn);
                if (explainedRules.insert(identity).second) {
                    result.explanation.push_back(markFact(std::make_shared<MapExpr>(
                    std::vector<MapEntry>{
                        {"__type", std::make_shared<StringExpr>(
                            "RuleProvenance")},
                        {"rule", std::make_shared<StringExpr>(node.rule)},
                        {"start_line", std::make_shared<NumberExpr>(
                            node.span.startLine)},
                        {"start_column", std::make_shared<NumberExpr>(
                            node.span.startColumn)},
                        {"end_line", std::make_shared<NumberExpr>(
                            node.span.endLine)},
                        {"end_column", std::make_shared<NumberExpr>(
                            node.span.endColumn)}}), "RuleProvenance"));
                }
            }
            pending.insert(
                pending.end(), node.parents.begin(), node.parents.end());
        }
        return result;
    };

    const Summary positive = summarize(supporting, positiveEvaluation);
    const Summary negative = summarize(opposing, negativeEvaluation);
    const bool hasPositive = !supporting.empty();
    const bool hasNegative = !opposing.empty();
    const std::string truth =
        hasPositive && hasNegative ? "both" :
        hasPositive ? "proved" :
        hasNegative ? "disproved" : "unknown";

    std::vector<std::shared_ptr<Expr>> explanation =
        positive.explanation;
    explanation.insert(
        explanation.end(),
        negative.explanation.begin(),
        negative.explanation.end());

    return markFact(std::make_shared<MapExpr>(std::vector<MapEntry>{
        {"__type", std::make_shared<StringExpr>("DerivationResult")},
        {"conclusion", callAsFact(query)},
        {"truth_status", std::make_shared<StringExpr>(truth)},
        {"exact", std::make_shared<BoolExpr>(true)},
        {"fuzzy_degree", std::make_shared<NilExpr>()},
        {"confidence", std::make_shared<NilExpr>()},
        {"probability", std::make_shared<NilExpr>()},
        {"similarity", std::make_shared<NilExpr>()},
        {"contradictory", std::make_shared<BoolExpr>(
            hasPositive && hasNegative)},
        {"supporting_facts", factIdArray(positive.factIds)},
        {"opposing_facts", factIdArray(negative.factIds)},
        {"supporting_rules", stringArray(positive.rules)},
        {"opposing_rules", stringArray(negative.rules)},
        {"support_count", std::make_shared<NumberExpr>(
            static_cast<double>(supporting.size()))},
        {"opposition_count", std::make_shared<NumberExpr>(
            static_cast<double>(opposing.size()))},
        {"explanation_path", std::make_shared<ArrayExpr>(
            std::move(explanation))}}), "DerivationResult");
}

bool Interpreter::evalReasoningContrary(
    const TermExpr& term,
    const Env& env,
    std::shared_ptr<Expr>& out) {
    std::shared_ptr<Expr> positiveValue;
    std::shared_ptr<Expr> negativeValue;
    for (std::size_t i = 0; i < term.args.size(); ++i) {
        const auto& argument = term.args[i];
        std::shared_ptr<Expr> resolved;
        if (!evalExprValue(argument.value, env, resolved)) return false;
        if (argument.name == "positive" ||
            (argument.name.empty() && i == 0)) {
            positiveValue = std::move(resolved);
        } else if (argument.name == "negative" ||
                   (argument.name.empty() && i == 1)) {
            negativeValue = std::move(resolved);
        }
    }
    const auto positive =
        std::dynamic_pointer_cast<StringExpr>(positiveValue);
    const auto negative =
        std::dynamic_pointer_cast<StringExpr>(negativeValue);
    if (!positive || positive->value.empty() ||
        !negative || negative->value.empty()) {
        throw InterpreterError(
            "Reasoning.contrary expects non-empty string predicates "
            "'positive' and 'negative'");
    }
    if (positive->value == negative->value) {
        throw InterpreterError(
            "Reasoning.contrary predicates must be different");
    }
    const SymbolId positiveId = symbolIdForName(positive->value);
    const auto existing = contraries_.find(positiveId);
    if (existing != contraries_.end() &&
        existing->second != negative->value) {
        throw InterpreterError(
            "Reasoning.contrary already maps '" + positive->value +
            "' to '" + existing->second + "'");
    }
    contraries_[positiveId] = negative->value;
    out = markFact(std::make_shared<MapExpr>(std::vector<MapEntry>{
        {"__type", std::make_shared<StringExpr>(
            "ContraryRegistration")},
        {"positive", positive->clone()},
        {"negative", negative->clone()}}), "ContraryRegistration");
    return true;
}

bool Interpreter::evalReasoningProve(
    const TermExpr& term,
    const Env& env,
    std::shared_ptr<Expr>& out) {
    const Arg* queryArgument = nullptr;
    for (std::size_t i = 0; i < term.args.size(); ++i) {
        if (term.args[i].name == "query" ||
            (term.args[i].name.empty() && i == 0)) {
            queryArgument = &term.args[i];
            break;
        }
    }
    if (!queryArgument) {
        throw InterpreterError("Reasoning.prove expects 'query'");
    }

    Call query;
    if (const auto queryTerm =
            std::dynamic_pointer_cast<TermExpr>(queryArgument->value)) {
        if (queryTerm->builtinId != BuiltinId::Unknown) {
            throw InterpreterError(
                "Reasoning.prove expects a relational predicate, not a builtin");
        }
        query = Call(queryTerm->name, {});
        query.nameId = queryTerm->nameId;
        for (const auto& argument : queryTerm->args) {
            std::shared_ptr<Expr> value;
            if (!evalExprValue(argument.value, env, value) ||
                !isGroundLiteral(value)) {
                throw InterpreterError(
                    "Reasoning.prove query arguments must be ground");
            }
            query.args.emplace_back(argument.name, argument.nameId, std::move(value));
        }
    } else {
        std::shared_ptr<Expr> resolved;
        if (!evalExprValue(queryArgument->value, env, resolved)) {
            return false;
        }
        const auto map = std::dynamic_pointer_cast<MapExpr>(resolved);
        const auto type = std::dynamic_pointer_cast<StringExpr>(
            reasoningMapValue(
                resolved,
                internalSymbolString(InternalSymbolKind::Type)));
        if (!map || !type || type->value.empty()) {
            throw InterpreterError(
                "Reasoning.prove expects a typed fact query");
        }
        query = Call(type->value, {});
        for (const auto& entry : map->entries) {
            if (entry.keyId == InternalSymbol::TypeId ||
                entry.keyId == InternalSymbol::ParentId) continue;
            if (!isGroundLiteral(entry.value)) {
                throw InterpreterError(
                    "Reasoning.prove query arguments must be ground");
            }
            query.args.emplace_back(entry.key, entry.keyId, entry.value->clone());
        }
    }

    if (!findClauses(query.name, query.nameId) &&
        !memory_.hasActiveRelation(query.name, query.nameId) &&
        !contraries_.count(query.nameId)) {
        throw InterpreterError(
            "Unknown reasoning predicate '" + query.name + "'");
    }

    std::vector<TableBinding> supporting;
    std::shared_ptr<TableEvaluation> positiveEvaluation;
    if (!tableCallAnswers(
            query,
            env,
            supporting,
            &positiveEvaluation,
            false)) {
        throw InterpreterError(
            "Reasoning.prove requires a pure relational predicate: '" +
            query.name + "'");
    }

    std::vector<TableBinding> opposing;
    std::shared_ptr<TableEvaluation> negativeEvaluation;
    const auto contrary = contraries_.find(query.nameId);
    if (contrary != contraries_.end()) {
        Call negative(contrary->second, {});
        negative.args.reserve(query.args.size());
        for (const auto& argument : query.args) {
            negative.args.emplace_back(
                argument.name, argument.value->clone());
        }
        if (findClauses(negative.name, negative.nameId) ||
            memory_.hasActiveRelation(negative.name, negative.nameId)) {
            if (!tableCallAnswers(
                    negative,
                    env,
                    opposing,
                    &negativeEvaluation,
                    false)) {
                throw InterpreterError(
                    "Contrary predicate '" + negative.name +
                    "' is not a pure relational predicate");
            }
        }
    }

    out = materializeDerivationResult(
        query,
        supporting,
        opposing,
        positiveEvaluation,
        negativeEvaluation);
    return true;
}

bool Interpreter::evalReasoningGrade(
    const TermExpr& term,
    const Env& env,
    std::shared_ptr<Expr>& out,
    const std::shared_ptr<MapExpr>& exact) {
    std::shared_ptr<Expr> evidenceValue;
    std::shared_ptr<Expr> profileValue;
    std::shared_ptr<Expr> conclusionValue;
    std::shared_ptr<Expr> probabilityValue;
    std::shared_ptr<Expr> similarityValue;
    for (std::size_t i = 0; i < term.args.size(); ++i) {
        const auto& argument = term.args[i];
        if (argument.name == "query") continue;
        std::shared_ptr<Expr> value;
        if (argument.name == "conclusion") {
            // A conclusion is data, not an evaluation request. Preserve a
            // predicate-shaped term as a typed fact value instead of invoking
            // a rule with the same name.
            if (const auto conclusion =
                    std::dynamic_pointer_cast<TermExpr>(argument.value)) {
                std::vector<MapEntry> fields;
                fields.emplace_back(
                    "__type",
                    std::make_shared<StringExpr>(conclusion->name));
                for (const auto& field : conclusion->args) {
                    std::shared_ptr<Expr> resolved;
                    if (!evalExprValue(field.value, env, resolved)) return false;
                    fields.emplace_back(field.name, field.nameId, std::move(resolved));
                }
                value = markFact(
                    std::make_shared<MapExpr>(std::move(fields)),
                    conclusion->name);
            }
        }
        if (!value && !evalExprValue(argument.value, env, value)) return false;
        if (argument.name == "evidence" ||
            (argument.name.empty() && i == 0)) {
            evidenceValue = std::move(value);
        } else if (argument.name == "profile") {
            profileValue = std::move(value);
        } else if (argument.name == "conclusion") {
            conclusionValue = std::move(value);
        } else if (argument.name == "probability") {
            probabilityValue = std::move(value);
        } else if (argument.name == "similarity") {
            similarityValue = std::move(value);
        }
    }

    if (exact && !conclusionValue) {
        conclusionValue = reasoningMapValue(exact, "conclusion");
    }
    if (!conclusionValue) conclusionValue = std::make_shared<NilExpr>();

    if (profileValue) {
        const auto profileType = std::dynamic_pointer_cast<StringExpr>(
            reasoningMapValue(profileValue, "__type"));
        if (!profileType || profileType->value != "ReasoningProfile") {
            throw InterpreterError(
                "Reasoning.grade profile must be ReasoningProfile(...)");
        }
        const auto requirePolicy =
            [&](const std::string& field,
                std::initializer_list<const char*> allowed) {
                const auto value = std::dynamic_pointer_cast<StringExpr>(
                    reasoningMapValue(profileValue, field));
                if (!value) return;
                for (const char* candidate : allowed) {
                    if (value->value == candidate) return;
                }
                throw InterpreterError(
                    "Unsupported ReasoningProfile " + field +
                    " policy '" + value->value + "'");
            };
        requirePolicy("conjunction", {"minimum"});
        requirePolicy("disjunction", {"maximum"});
        requirePolicy("evidence_aggregation", {"maximum"});
        requirePolicy("negation", {"standard", "one_minus"});
    } else {
        profileValue = markFact(std::make_shared<MapExpr>(
            std::vector<MapEntry>{
                {"__type", std::make_shared<StringExpr>(
                    "ReasoningProfile")},
                {"name", std::make_shared<StringExpr>("default")},
                {"conjunction", std::make_shared<StringExpr>("minimum")},
                {"disjunction", std::make_shared<StringExpr>("maximum")},
                {"evidence_aggregation", std::make_shared<StringExpr>(
                    "maximum")},
                {"negation", std::make_shared<StringExpr>("one_minus")}}),
            "ReasoningProfile");
    }

    std::vector<std::shared_ptr<Expr>> evidenceItems;
    if (evidenceValue) {
        const auto array =
            std::dynamic_pointer_cast<ArrayExpr>(evidenceValue);
        if (!array) {
            throw InterpreterError(
                "Reasoning.grade evidence must be an array");
        }
        evidenceItems = array->items;
    }

    bool hasSupport = false;
    bool hasOpposition = false;
    double support = 0.0;
    double opposition = 0.0;
    double confidence = 0.0;
    for (const auto& evidence : evidenceItems) {
        const auto type = std::dynamic_pointer_cast<StringExpr>(
            reasoningMapValue(evidence, "__type"));
        if (!type ||
            (type->value != "Evidence" &&
             type->value != "FuzzyMembership" &&
             type->value != "Comparison")) {
            throw InterpreterError(
                "Reasoning.grade entries must be Evidence(...), "
                "FuzzyMembership(...), or Comparison(...)");
        }
        if (type->value == "Comparison") {
            const auto similarity = std::dynamic_pointer_cast<NumberExpr>(
                reasoningMapValue(evidence, "similarity"));
            const auto confidenceValue = std::dynamic_pointer_cast<NumberExpr>(
                reasoningMapValue(evidence, "relationalConfidence"));
            const auto contradictory = std::dynamic_pointer_cast<BoolExpr>(
                reasoningMapValue(evidence, "contradictory"));
            const auto conflicting = std::dynamic_pointer_cast<ArrayExpr>(
                reasoningMapValue(evidence, "conflictingFields"));
            if (!similarity || !finiteUnitInterval(similarity->value)) {
                throw InterpreterError(
                    "Comparison evidence similarity must be finite and between 0 and 1");
            }
            const double reliability = confidenceValue
                ? confidenceValue->value : 1.0;
            if (!finiteUnitInterval(reliability)) {
                throw InterpreterError(
                    "Comparison evidence relationalConfidence must be between 0 and 1");
            }
            const bool opposed = (contradictory && contradictory->value) ||
                (conflicting && !conflicting->items.empty());
            confidence = std::max(confidence, reliability);
            if (opposed) {
                hasOpposition = true;
                opposition = std::max(opposition,
                    (1.0 - similarity->value) * reliability);
            }
            if (similarity->value > 0.0) {
                hasSupport = true;
                support = std::max(support, similarity->value * reliability);
            }
            continue;
        }
        const auto degree = std::dynamic_pointer_cast<NumberExpr>(
            reasoningMapValue(evidence, "degree"));
        const auto reliabilityValue =
            std::dynamic_pointer_cast<NumberExpr>(
                reasoningMapValue(evidence, "reliability"));
        const double reliability =
            reliabilityValue ? reliabilityValue->value : 1.0;
        if (!degree || !finiteUnitInterval(degree->value) ||
            !finiteUnitInterval(reliability)) {
            throw InterpreterError(
                "Reasoning grades and reliability must be finite values "
                "between 0 and 1");
        }
        bool opposing = false;
        if (const auto polarity = std::dynamic_pointer_cast<StringExpr>(
                reasoningMapValue(evidence, "polarity"))) {
            opposing =
                polarity->value == "oppose" ||
                polarity->value == "opposing" ||
                polarity->value == "refute";
            if (!opposing && polarity->value != "support" &&
                polarity->value != "supporting") {
                throw InterpreterError(
                    "Evidence polarity must be 'support' or 'oppose'");
            }
        } else if (const auto supports =
                       std::dynamic_pointer_cast<BoolExpr>(
                           reasoningMapValue(evidence, "supports"))) {
            opposing = !supports->value;
        }
        const double discounted = degree->value * reliability;
        confidence = std::max(confidence, reliability);
        if (opposing) {
            hasOpposition = true;
            opposition = std::max(opposition, discounted);
        } else {
            hasSupport = true;
            support = std::max(support, discounted);
        }
    }

    const auto validatedOptionalGrade =
        [&](const std::shared_ptr<Expr>& value,
            const std::string& name) -> std::shared_ptr<Expr> {
            if (!value) return std::make_shared<NilExpr>();
            const auto number =
                std::dynamic_pointer_cast<NumberExpr>(value);
            if (!number || !finiteUnitInterval(number->value)) {
                throw InterpreterError(
                    "Reasoning " + name +
                    " must be a finite value between 0 and 1");
            }
            return number->clone();
        };

    std::string truth = "unknown";
    if (exact) {
        if (const auto status = std::dynamic_pointer_cast<StringExpr>(
                reasoningMapValue(exact, "truth_status"))) {
            truth = status->value;
        }
    }
    const std::string recommendation =
        support > opposition ? "recommend" :
        opposition > support ? "reject" : "undetermined";

    std::vector<MapEntry> resultEntries;
    if (exact) {
        resultEntries.reserve(exact->entries.size() + 8);
        for (const auto& entry : exact->entries) {
            resultEntries.emplace_back(entry.key, entry.keyId, entry.value->clone());
        }
    }
    reasoningSetValue(resultEntries, "__type",
        std::make_shared<StringExpr>("DerivationResult"));
    reasoningSetValue(resultEntries, "conclusion", conclusionValue->clone());
    reasoningSetValue(resultEntries, "truth_status",
        std::make_shared<StringExpr>(truth));
    reasoningSetValue(resultEntries, "exact",
        std::make_shared<BoolExpr>(static_cast<bool>(exact)));
    reasoningSetValue(resultEntries, "fuzzy_degree", hasSupport
        ? std::shared_ptr<Expr>(std::make_shared<NumberExpr>(support))
        : std::shared_ptr<Expr>(std::make_shared<NilExpr>()));
    reasoningSetValue(resultEntries, "support_degree", hasSupport
        ? std::shared_ptr<Expr>(std::make_shared<NumberExpr>(support))
        : std::shared_ptr<Expr>(std::make_shared<NilExpr>()));
    reasoningSetValue(resultEntries, "opposition_degree", hasOpposition
        ? std::shared_ptr<Expr>(std::make_shared<NumberExpr>(opposition))
        : std::shared_ptr<Expr>(std::make_shared<NilExpr>()));
    reasoningSetValue(resultEntries, "confidence", evidenceItems.empty()
        ? std::shared_ptr<Expr>(std::make_shared<NilExpr>())
        : std::shared_ptr<Expr>(std::make_shared<NumberExpr>(confidence)));
    reasoningSetValue(resultEntries, "probability",
        validatedOptionalGrade(probabilityValue, "probability"));
    reasoningSetValue(resultEntries, "similarity",
        validatedOptionalGrade(similarityValue, "similarity"));
    reasoningSetValue(resultEntries, "contradictory",
        std::make_shared<BoolExpr>(
            truth == "both" || (hasSupport && hasOpposition)));
    reasoningSetValue(resultEntries, "recommendation",
        std::make_shared<StringExpr>(recommendation));
    reasoningSetValue(resultEntries, "profile", profileValue->clone());
    reasoningSetValue(resultEntries, "evidence",
        std::make_shared<ArrayExpr>(evidenceItems));
    out = markFact(
        std::make_shared<MapExpr>(std::move(resultEntries)),
        "DerivationResult");
    return true;
}

bool Interpreter::evalReasoningBuiltin(
    const TermExpr& term,
    const Env& env,
    std::shared_ptr<Expr>& out) {
    switch (term.builtinId) {
        case BuiltinId::ReasoningContrary:
            return evalReasoningContrary(term, env, out);
        case BuiltinId::ReasoningProve:
            return evalReasoningProve(term, env, out);
        case BuiltinId::ReasoningGrade:
            return evalReasoningGrade(term, env, out);
        case BuiltinId::ReasoningDecide: {
            std::shared_ptr<Expr> exactValue;
            if (!evalReasoningProve(term, env, exactValue)) return false;
            const auto exact =
                std::dynamic_pointer_cast<MapExpr>(exactValue);
            return evalReasoningGrade(term, env, out, exact);
        }
        default:
            return false;
    }
}

} // namespace Felidae
