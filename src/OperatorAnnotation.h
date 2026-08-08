#pragma once

#include "AST.h"
#include <stdexcept>

namespace Felidae {

struct ParsedOperatorAnnotation {
    BuiltinId kind = BuiltinId::Unknown;
    std::string operatorName;
    std::string pattern;
    std::vector<std::string> anchors;
    std::vector<OperatorTypeBinding> captures;
    std::vector<OperatorTypeBinding> factors;
    std::vector<OperatorTypeBinding> produces;
    std::string resultType;
    OperatorPrecedence precedence = OperatorPrecedence::Relationship;
    OperatorAssociativity associativity = OperatorAssociativity::None;
    OperatorFixity fixity = OperatorFixity::Infix;
    OperatorVisibility visibility = OperatorVisibility::Private;
    OperatorCardinality cardinality = OperatorCardinality::One;
    OperatorEffect effect = OperatorEffect::Pure;
    bool hasPattern = false;
    bool hasPrecedence = false;
    bool hasAssociativity = false;
    bool hasFixity = false;
    bool hasVisibility = false;
    bool hasCardinality = false;
    bool hasEffects = false;
    bool hasResult = false;
    bool hasFactor = false;
    bool hasFactors = false;
};

inline ParsedOperatorAnnotation decodeOperatorAnnotation(const Call& annotation) {
    if (annotation.builtinId != BuiltinId::OverloadAnnotation &&
        annotation.builtinId != BuiltinId::MixfixAnnotation &&
        annotation.builtinId != BuiltinId::MatcherAnnotation) {
        throw std::runtime_error("Call is not an operator annotation");
    }
    const auto argument = [&](std::string_view name) -> std::shared_ptr<Expr> {
        const SymbolId wanted = symbolIdForName(name);
        for (const auto& arg : annotation.args) {
            if (arg.nameId == wanted && arg.name == name) return arg.value;
        }
        return {};
    };
    const auto text = [&](std::string_view name, bool required = false) -> std::string {
        auto value = argument(name);
        if (auto literal = std::dynamic_pointer_cast<StringExpr>(value)) return literal->value;
        if (auto symbol = std::dynamic_pointer_cast<VarExpr>(value)) return symbol->name;
        if (required) throw std::runtime_error(
            "@" + annotation.name + " requires '" + std::string(name) + "'");
        return {};
    };
    const auto bindings = [&](std::string_view name, bool allowArray = false) {
        std::vector<OperatorTypeBinding> result;
        auto value = argument(name);
        if (!value) return result;
        const auto append = [&](const std::shared_ptr<MapExpr>& map) {
            if (!map) {
                throw std::runtime_error(
                    "@" + annotation.name + " '" + std::string(name) +
                    "' requires named type bindings");
            }
            for (const auto& entry : map->entries) {
                auto type = std::dynamic_pointer_cast<VarExpr>(entry.value);
                if (!type || !isFelidaeTypeAnnotationName(type->name)) {
                    throw std::runtime_error(
                        "@" + annotation.name + " binding '" + entry.key + "' requires a type");
                }
                for (const auto& existing : result) {
                    if (existing.nameId == entry.keyId && existing.name == entry.key) {
                        throw std::runtime_error(
                            "@" + annotation.name + " repeats binding '" + entry.key + "'");
                    }
                }
                result.push_back({
                    entry.key,
                    entry.keyId,
                    type->name,
                    symbolIdForName(type->name),
                    languageTypeIdForName(type->name)});
            }
        };
        if (auto map = std::dynamic_pointer_cast<MapExpr>(value)) {
            append(map);
            return result;
        }
        if (allowArray) {
            if (auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) {
                for (const auto& item : array->items) {
                    auto map = std::dynamic_pointer_cast<MapExpr>(item);
                    if (!map || map->entries.size() != 1) {
                        throw std::runtime_error(
                            "@" + annotation.name + " '" + std::string(name) +
                            "' array entries must each be 'binding: Type'");
                    }
                    append(map);
                }
                return result;
            }
        }
        throw std::runtime_error(
            "@" + annotation.name + " '" + std::string(name) +
            (allowArray ? "' must be a named binding or binding array" : "' must be a map"));
    };

    ParsedOperatorAnnotation parsed;
    parsed.kind = annotation.builtinId;
    parsed.hasPattern = static_cast<bool>(argument("pattern"));
    parsed.hasPrecedence = static_cast<bool>(argument("precedence"));
    parsed.hasAssociativity = static_cast<bool>(argument("associativity"));
    parsed.hasFixity = static_cast<bool>(argument("type"));
    parsed.hasVisibility = static_cast<bool>(argument("visibility"));
    parsed.hasCardinality = static_cast<bool>(argument("cardinality"));
    parsed.hasEffects = static_cast<bool>(argument("effects"));
    parsed.hasResult = static_cast<bool>(argument("result"));
    parsed.hasFactor = static_cast<bool>(argument("factor"));
    parsed.hasFactors = static_cast<bool>(argument("factors"));
    parsed.operatorName = text(
        "operator", annotation.builtinId != BuiltinId::MixfixAnnotation);
    parsed.pattern = text("pattern");
    if (annotation.builtinId == BuiltinId::MixfixAnnotation) {
        if (argument("operator")) {
            throw std::runtime_error(
                "@mixfix derives its identity from the pattern; remove 'operator'");
        }
        if (argument("captures")) {
            throw std::runtime_error(
                "@mixfix declares captures inside 'pattern'; remove 'captures'");
        }
        if (parsed.pattern.empty()) {
            throw std::runtime_error("@mixfix requires a typed pattern string");
        }
        std::string normalized;
        std::size_t cursor = 0;
        while (cursor < parsed.pattern.size()) {
            const std::size_t open = parsed.pattern.find('{', cursor);
            if (open == std::string::npos) {
                normalized.append(parsed.pattern, cursor, std::string::npos);
                break;
            }
            normalized.append(parsed.pattern, cursor, open - cursor);
            const std::size_t close = parsed.pattern.find('}', open + 1);
            if (close == std::string::npos) {
                throw std::runtime_error("@mixfix pattern has an unterminated capture");
            }
            std::string declaration = parsed.pattern.substr(open + 1, close - open - 1);
            const std::size_t colon = declaration.find(':');
            if (colon == std::string::npos) {
                throw std::runtime_error("@mixfix captures must use '{name: type}'");
            }
            auto trim = [](std::string value) {
                const auto first = value.find_first_not_of(" \t");
                const auto last = value.find_last_not_of(" \t");
                if (first == std::string::npos) return std::string{};
                return value.substr(first, last - first + 1);
            };
            const std::string name = trim(declaration.substr(0, colon));
            const std::string type = trim(declaration.substr(colon + 1));
            if (name.empty() || type.empty() || !isFelidaeTypeAnnotationName(type)) {
                throw std::runtime_error("@mixfix capture requires a valid '{name: type}' declaration");
            }
            const SymbolId nameId = symbolIdForName(name);
            for (const auto& existing : parsed.captures) {
                if (existing.nameId == nameId) {
                    throw std::runtime_error("@mixfix repeats capture '" + name + "'");
                }
            }
            parsed.captures.push_back({name, nameId, type, symbolIdForName(type),
                                       languageTypeIdForName(type)});
            normalized += "{" + name + "}";
            cursor = close + 1;
        }
        parsed.pattern = std::move(normalized);
        if (parsed.operatorName.empty()) {
            // A mixfix declaration is identified by its pattern.  Derive a
            // stable display/lookup name from its first literal anchor so the
            // annotation does not need a redundant operator field.
            std::size_t anchorStart = 0;
            while (anchorStart < parsed.pattern.size()) {
                while (anchorStart < parsed.pattern.size() &&
                       (parsed.pattern[anchorStart] == ' ' ||
                        parsed.pattern[anchorStart] == '\t')) {
                    ++anchorStart;
                }
                if (anchorStart >= parsed.pattern.size() || parsed.pattern[anchorStart] != '{') break;
                const auto close = parsed.pattern.find('}', anchorStart + 1);
                if (close == std::string::npos) {
                    throw std::runtime_error("@mixfix pattern has an unterminated capture");
                }
                anchorStart = close + 1;
            }
            const auto anchorEnd = parsed.pattern.find_first_of(" \t{", anchorStart);
            if (anchorStart >= parsed.pattern.size() || anchorStart == anchorEnd) {
                throw std::runtime_error(
                    "@mixfix pattern must contain a literal operator anchor");
            }
            parsed.operatorName = parsed.pattern.substr(
                anchorStart,
                anchorEnd == std::string::npos
                    ? std::string::npos : anchorEnd - anchorStart);
        }
    }
    if (annotation.builtinId != BuiltinId::MixfixAnnotation) {
        parsed.captures = bindings("captures");
    }
    if (parsed.hasFactor && parsed.hasFactors) {
        throw std::runtime_error("@" + annotation.name + " cannot use both 'factor' and 'factors'");
    }
    parsed.factors = argument("factor") ? bindings("factor") : bindings("factors", true);
    parsed.produces = bindings("produces", true);
    parsed.resultType = text("result");

    const auto precedence = text("precedence");
    if (precedence == "additive") parsed.precedence = OperatorPrecedence::Additive;
    else if (precedence == "multiplicative") parsed.precedence = OperatorPrecedence::Multiplicative;
    else if (precedence == "ordering") parsed.precedence = OperatorPrecedence::Ordering;
    else if (precedence == "prefix") parsed.precedence = OperatorPrecedence::Prefix;
    else if (precedence == "postfix") parsed.precedence = OperatorPrecedence::Postfix;
    else if (precedence == "relationship" || precedence.empty()) {}
    else throw std::runtime_error("Unknown operator precedence '" + precedence + "'");

    const auto associativity = text("associativity");
    if (associativity == "left") parsed.associativity = OperatorAssociativity::Left;
    else if (associativity == "right") parsed.associativity = OperatorAssociativity::Right;
    else if (associativity == "none" || associativity.empty()) {}
    else throw std::runtime_error("Unknown operator associativity '" + associativity + "'");

    const auto fixity = text("type");
    if (fixity == "infix" || fixity.empty()) {}
    else if (fixity == "prefix") parsed.fixity = OperatorFixity::Prefix;
    else if (fixity == "postfix") parsed.fixity = OperatorFixity::Postfix;
    else if (fixity == "mixfix") parsed.fixity = OperatorFixity::Mixfix;
    else throw std::runtime_error("Unknown operator type '" + fixity + "'");

    const auto visibility = text("visibility");
    if (visibility == "public") parsed.visibility = OperatorVisibility::Public;
    else if (visibility != "private" && !visibility.empty()) {
        throw std::runtime_error("Unknown operator visibility '" + visibility + "'");
    }
    const auto cardinality = text("cardinality");
    if (cardinality == "one" || cardinality.empty()) {}
    else if (cardinality == "optional") parsed.cardinality = OperatorCardinality::Optional;
    else if (cardinality == "many") parsed.cardinality = OperatorCardinality::Many;
    else throw std::runtime_error("Unknown operator cardinality '" + cardinality + "'");
    const auto effects = text("effects");
    if (effects == "pure" || effects.empty()) {}
    else if (effects == "impure") parsed.effect = OperatorEffect::Impure;
    else throw std::runtime_error("Unknown operator effects '" + effects + "'");
    return parsed;
}

inline OperatorOverloadDefinition makeOperatorOverloadDefinition(
    const ParsedOperatorAnnotation& parsed,
    const OperatorPatternDefinition& pattern,
    std::string methodName,
    SymbolId methodId,
    std::string module) {
    OperatorOverloadDefinition overload;
    overload.operatorId = pattern.operatorId;
    overload.patternId = pattern.patternId;
    overload.methodId = methodId;
    overload.methodName = std::move(methodName);
    overload.captures = parsed.captures;
    overload.factors = parsed.factors;
    overload.resultType = parsed.resultType;
    overload.resultTypeId = parsed.resultType.empty() ? 0 : symbolIdForName(parsed.resultType);
    overload.resultLanguageTypeId = languageTypeIdForName(parsed.resultType);
    overload.cardinality = parsed.cardinality;
    overload.effect = parsed.effect;
    overload.visibility = parsed.visibility;
    overload.module = std::move(module);
    return overload;
}

inline OperatorMatcherDefinition makeOperatorMatcherDefinition(
    const ParsedOperatorAnnotation& parsed,
    const OperatorPatternDefinition& pattern,
    std::string methodName,
    SymbolId methodId,
    std::string module) {
    return OperatorMatcherDefinition{
        pattern.operatorId,
        pattern.patternId,
        methodId,
        std::move(methodName),
        parsed.captures,
        parsed.produces,
        parsed.visibility,
        std::move(module)};
}

} // namespace Felidae
