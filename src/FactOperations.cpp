#include "Interpreter.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <regex>
#include <string_view>

namespace Felidae {
namespace {

std::shared_ptr<Expr> fieldValue(const std::shared_ptr<Expr>& value,
                                 SymbolId fieldId,
                                 std::string_view field) {
    const auto map = std::dynamic_pointer_cast<MapExpr>(value);
    if (!map) return {};
    const auto found = std::find_if(map->entries.begin(), map->entries.end(),
        [&](const MapEntry& entry) {
            return entry.keyId == fieldId && entry.key == field;
        });
    return found == map->entries.end() ? std::shared_ptr<Expr>{} : found->value;
}

std::string asciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char byte) {
        const auto current = static_cast<unsigned char>(byte);
        return static_cast<char>(current >= 'A' && current <= 'Z'
            ? current - 'A' + 'a' : current);
    });
    return value;
}

bool matchesLike(std::string_view value, std::string_view pattern) {
    std::vector<unsigned char> previous(value.size() + 1);
    std::vector<unsigned char> current(value.size() + 1);
    previous[0] = 1;
    for (std::size_t patternIndex = 0; patternIndex < pattern.size(); ++patternIndex) {
        std::fill(current.begin(), current.end(), 0);
        char token = pattern[patternIndex];
        bool escaped = false;
        if (token == '\\') {
            if (++patternIndex == pattern.size())
                throw InterpreterError("Fact.search LIKE pattern has a trailing escape");
            token = pattern[patternIndex];
            escaped = true;
        }
        if (!escaped && token == '%') {
            current[0] = previous[0];
            for (std::size_t index = 1; index <= value.size(); ++index)
                current[index] = previous[index] || current[index - 1];
        } else {
            for (std::size_t index = 1; index <= value.size(); ++index) {
                current[index] = previous[index - 1] &&
                    ((!escaped && token == '_') || value[index - 1] == token);
            }
        }
        previous.swap(current);
    }
    return previous[value.size()] != 0;
}

} // namespace

std::shared_ptr<FactSelectionExpr> Interpreter::makeFactSelection(
    const std::string& type, const std::shared_ptr<MapExpr>& match) {
    auto selection = std::make_shared<FactSelectionExpr>(
        type, memory_.captureSnapshot());
    if (!match) return selection;
    selection->filters.reserve(match->entries.size());
    for (const auto& field : match->entries) {
        selection->filters.push_back(FactSelectionFilter{
            field.key, field.keyId, TokenId::EQUAL,
            field.value ? field.value->clone() : std::make_shared<NilExpr>()});
    }
    return selection;
}

std::shared_ptr<ArrayExpr> Interpreter::projectFacts(
    const std::shared_ptr<FactSelectionExpr>& selection,
    const ArrayExpr& fields) {
    std::vector<std::pair<std::string, SymbolId>> names;
    names.reserve(fields.items.size());
    for (const auto& item : fields.items) {
        const auto name = std::dynamic_pointer_cast<StringExpr>(item);
        if (!name || name->value.empty())
            throw InterpreterError("Fact.select fields must contain non-empty text names");
        names.emplace_back(name->value, symbolIdForName(name->value));
    }
    const auto rows = materializeFactSelection(selection);
    std::vector<std::shared_ptr<Expr>> projected;
    projected.reserve(rows->items.size());
    for (const auto& row : rows->items) {
        std::vector<MapEntry> entries;
        entries.reserve(names.size());
        for (const auto& [name, id] : names) {
            const auto value = fieldValue(row, id, name);
            if (!value)
                throw InterpreterError("Fact.select references missing field '" + name + "'");
            entries.emplace_back(name, id, value->clone());
        }
        projected.push_back(std::make_shared<MapExpr>(std::move(entries)));
    }
    return std::make_shared<ArrayExpr>(std::move(projected));
}

double Interpreter::aggregateFacts(
    const std::shared_ptr<FactSelectionExpr>& selection,
    const std::string& field,
    std::uint8_t operation) {
    if (operation > 3) throw InterpreterError("Fact aggregate operation is invalid");
    const SymbolId fieldId = symbolIdForName(field);
    const auto rows = materializeFactSelection(selection);
    std::vector<double> values;
    values.reserve(rows->items.size());
    for (const auto& row : rows->items) {
        const auto value = std::dynamic_pointer_cast<NumberExpr>(fieldValue(row, fieldId, field));
        if (!value || !std::isfinite(value->value))
            throw InterpreterError("Fact aggregate field '" + field + "' must contain finite numbers");
        values.push_back(value->value);
    }
    if (values.empty()) {
        if (operation == 0) return 0.0;
        throw InterpreterError("Fact aggregate requires at least one matching fact");
    }
    if (operation == 0) return std::accumulate(values.begin(), values.end(), 0.0);
    if (operation == 1) {
        return std::accumulate(values.begin(), values.end(), 0.0) /
            static_cast<double>(values.size());
    }
    if (operation == 2) return *std::min_element(values.begin(), values.end());
    return *std::max_element(values.begin(), values.end());
}

std::shared_ptr<ArrayExpr> Interpreter::searchFacts(
    const std::shared_ptr<FactSelectionExpr>& selection,
    const std::string& field,
    const MapExpr& options) {
    const auto option = [&](std::string_view name) -> std::shared_ptr<Expr> {
        const SymbolId id = symbolIdForName(name);
        const auto found = std::find_if(options.entries.begin(), options.entries.end(),
            [&](const MapEntry& entry) {
                return entry.keyId == id && entry.key == name;
            });
        return found == options.entries.end() ? std::shared_ptr<Expr>{} : found->value;
    };
    const auto textOption = [&](std::string_view name, bool required)
        -> std::optional<std::string> {
        const auto value = std::dynamic_pointer_cast<StringExpr>(option(name));
        if (!value && required)
            throw InterpreterError("Fact.search requires text option '" + std::string(name) + "'");
        return value ? std::optional<std::string>(value->value) : std::nullopt;
    };
    const auto numberOption = [&](std::string_view name) -> std::optional<double> {
        const auto value = option(name);
        if (!value) return std::nullopt;
        const auto number = std::dynamic_pointer_cast<NumberExpr>(value);
        if (!number || !std::isfinite(number->value))
            throw InterpreterError("Fact.search option '" + std::string(name) + "' must be finite numeric data");
        return number->value;
    };
    const auto typeOf = [](const std::shared_ptr<Expr>& value) -> std::optional<std::string> {
        if (const auto name = std::dynamic_pointer_cast<VarExpr>(value)) return name->name;
        if (const auto name = std::dynamic_pointer_cast<StringExpr>(value)) return name->value;
        if (const auto fact = std::dynamic_pointer_cast<MapExpr>(value); fact && !fact->factType.empty())
            return fact->factType;
        return std::nullopt;
    };

    const auto mode = textOption("type", true).value();
    const auto rows = materializeFactSelection(selection);
    const SymbolId fieldId = symbolIdForName(field);
    std::vector<std::shared_ptr<Expr>> matches;
    matches.reserve(rows->items.size());
    const auto appendMatching = [&](const auto& predicate) {
        for (const auto& row : rows->items) {
            const auto value = fieldValue(row, fieldId, field);
            if (value && predicate(value)) matches.push_back(row->clone());
        }
    };

    if (mode == "exact" || mode == "prefix" || mode == "suffix" ||
        mode == "contains" || mode == "like" || mode == "regex") {
        auto query = textOption("query", true).value();
        const auto caseMode = textOption("case", false).value_or("sensitive");
        if (caseMode != "sensitive" && caseMode != "insensitive")
            throw InterpreterError("Fact.search case must be sensitive or insensitive");
        if (query.size() > 1024)
            throw InterpreterError("Fact.search pattern exceeds 1024 bytes");
        const bool insensitive = caseMode == "insensitive";
        const bool foldText = insensitive && mode != "regex";
        if (foldText) query = asciiLower(std::move(query));
        std::optional<std::regex> expression;
        if (mode == "regex") {
            try {
                auto flags = std::regex_constants::ECMAScript | std::regex_constants::optimize;
                if (insensitive) flags |= std::regex_constants::icase;
                expression.emplace(query, flags);
            } catch (const std::regex_error&) {
                throw InterpreterError("Fact.search regular expression is invalid");
            }
        }
        appendMatching([&](const std::shared_ptr<Expr>& value) {
            const auto text = std::dynamic_pointer_cast<StringExpr>(value);
            if (!text) return false;
            auto candidate = foldText ? asciiLower(text->value) : text->value;
            if (candidate.size() > 64 * 1024)
                throw InterpreterError("Fact.search text exceeds 64 KiB");
            if (mode == "exact") return candidate == query;
            if (mode == "prefix") return candidate.starts_with(query);
            if (mode == "suffix") return candidate.ends_with(query);
            if (mode == "contains") return candidate.find(query) != std::string::npos;
            if (mode == "like") return matchesLike(candidate, query);
            return std::regex_search(candidate, *expression);
        });
    } else if (mode == "hierarchy") {
        const auto queryType = typeOf(option("query"));
        const auto direction = textOption("direction", true).value();
        const double includeSelf = numberOption("includeSelf").value_or(1.0);
        if (!queryType) throw InterpreterError("Fact.search hierarchy query must name a fact or class type");
        if (includeSelf != 0.0 && includeSelf != 1.0)
            throw InterpreterError("Fact.search includeSelf must be 0.0 or 1.0");
        appendMatching([&](const std::shared_ptr<Expr>& value) {
            const auto candidate = typeOf(value);
            if (!candidate || (includeSelf == 0.0 && *candidate == *queryType)) return false;
            if (direction == "descendants") return memory_.isCompatibleType(*candidate, *queryType);
            if (direction == "ancestors") return memory_.isCompatibleType(*queryType, *candidate);
            if (direction == "related") {
                const auto left = typeAncestorDistances(*candidate);
                const auto right = typeAncestorDistances(*queryType);
                return std::any_of(left.begin(), left.end(), [&](const auto& ancestor) {
                    return right.contains(ancestor.first);
                });
            }
            throw InterpreterError("Fact.search hierarchy direction is invalid");
        });
    } else if (mode == "degree") {
        const auto query = numberOption("query");
        const auto tolerance = numberOption("tolerance");
        const auto minimum = numberOption("minimum");
        const auto maximum = numberOption("maximum");
        const bool closeness = query && tolerance;
        const bool range = minimum || maximum;
        if (closeness == range || (query.has_value() != tolerance.has_value()))
            throw InterpreterError("Fact.search degree requires query+tolerance or range bounds");
        if (tolerance && *tolerance < 0.0)
            throw InterpreterError("Fact.search tolerance must be non-negative");
        if (minimum && maximum && *minimum > *maximum)
            throw InterpreterError("Fact.search degree minimum exceeds maximum");
        appendMatching([&](const std::shared_ptr<Expr>& value) {
            const auto number = std::dynamic_pointer_cast<NumberExpr>(value);
            if (!number || !std::isfinite(number->value)) return false;
            if (closeness) return std::fabs(number->value - *query) <= *tolerance;
            return (!minimum || number->value >= *minimum) &&
                (!maximum || number->value <= *maximum);
        });
    } else {
        throw InterpreterError("Fact.search type is invalid");
    }
    return std::make_shared<ArrayExpr>(std::move(matches));
}

std::shared_ptr<ArrayExpr> Interpreter::joinFacts(
    const std::string& leftType, const std::string& rightType,
    const std::string& leftField, const std::string& rightField,
    std::uint8_t kind) {
    if (kind > 3) throw InterpreterError("Fact.join kind is invalid");
    const auto leftSelection = makeFactSelection(leftType);
    const auto rightSelection = makeFactSelection(rightType);
    const auto leftRows = materializeFactSelection(leftSelection);
    const auto rightRows = materializeFactSelection(rightSelection);
    const SymbolId leftId = symbolIdForName(leftField);
    const SymbolId rightId = symbolIdForName(rightField);
    std::unordered_set<std::uint64_t> matchedRight;
    std::vector<std::shared_ptr<Expr>> joined;
    const auto append = [&](std::shared_ptr<Expr> left, std::shared_ptr<Expr> right) {
        auto value = std::make_shared<MapExpr>(std::vector<MapEntry>{
            MapEntry{"left", left ? std::move(left) : std::make_shared<NilExpr>()},
            MapEntry{"right", right ? std::move(right) : std::make_shared<NilExpr>()}});
        value->factType = "Joined";
        joined.push_back(std::move(value));
    };
    for (const auto& left : leftRows->items) {
        const auto leftValue = fieldValue(left, leftId, leftField);
        bool matchedLeft = false;
        if (!leftValue) {
            if (kind == 1 || kind == 3) append(left->clone(), {});
            continue;
        }
        const auto candidates = memory_.currentFactIndexes(
            memory_.selectionIndexes(rightType, rightField, leftValue,
                                     rightSelection->snapshotGeneration),
            rightSelection->snapshotGeneration);
        for (const auto index : candidates) {
            const auto right = memory_.factValue(
                index, rightSelection->snapshotGeneration);
            const auto rightValue = fieldValue(right, rightId, rightField);
            if (!rightValue || !exprContainsLiteral(leftValue, rightValue)) continue;
            matchedLeft = true;
            if (right->factIdentity != 0) matchedRight.insert(right->factIdentity);
            append(left->clone(), right->clone());
        }
        if (!matchedLeft && (kind == 1 || kind == 3)) append(left->clone(), {});
    }
    if (kind == 2 || kind == 3) {
        for (const auto& right : rightRows->items) {
            const auto fact = std::dynamic_pointer_cast<MapExpr>(right);
            if (!fact || !matchedRight.contains(fact->factIdentity)) {
                append({}, right->clone());
            }
        }
    }
    return std::make_shared<ArrayExpr>(std::move(joined));
}

} // namespace Felidae
