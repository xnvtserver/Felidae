#pragma once

#include "Token.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Felidae {

using OperatorId = std::uint32_t;
using PatternId = std::uint32_t;

enum class CoreOperator : OperatorId {
    Unknown = 0,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    StrictEqual,
    StrictNotEqual,
    Then,
    UnaryPlus,
    UnaryMinus,
    LogicalAnd,
    LogicalOr,
    LogicalNot
};

enum class OperatorPrecedence : std::uint8_t {
    Control = 10,
    LogicalOr = 20,
    LogicalAnd = 30,
    Relationship = 40,
    Ordering = 50,
    Pipeline = 55,
    Additive = 60,
    Multiplicative = 70,
    Prefix = 80,
    Postfix = 85
};

enum class OperatorAssociativity : std::uint8_t { None, Left, Right };
enum class OperatorFixity : std::uint8_t { Prefix, Infix, Postfix, Mixfix };
enum class OperatorVisibility : std::uint8_t { Private, Public };
enum class OperatorCardinality : std::uint8_t { One, Optional, Many };
enum class OperatorEffect : std::uint8_t { Pure, Impure };

struct OperatorTypeBinding {
    std::string name;
    SymbolId nameId = 0;
    std::string type;
    SymbolId typeId = 0;
    LanguageTypeId languageTypeId = LanguageTypeId::Unknown;
};

// Pattern anchors are compiled once when an annotation is registered. They are
// model-local integer sequences, never a second token category.
struct PatternLexeme {
    std::string spelling;
    SymbolId symbolId = 0;
    std::vector<int> pieceIds;
};

struct PieceSequenceHash {
    std::size_t operator()(const std::vector<int>& sequence) const noexcept {
        std::size_t hash = sequence.size();
        for (const int id : sequence) {
            hash ^= static_cast<std::size_t>(id) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        }
        return hash;
    }
};

struct OperatorPatternDefinition {
    OperatorId operatorId = 0;
    PatternId patternId = 0;
    std::string operatorName;
    SymbolId operatorNameId = 0;
    std::string pattern;
    std::vector<std::string> anchors;
    std::vector<std::vector<PatternLexeme>> anchorLexemes;
    std::vector<std::string> captureNames;
    std::vector<std::string> captureTypeNames;
    // One entry per capture. Empty means the next capture is adjacent and the
    // current capture consumes one primary expression.
    std::vector<std::string> followingAnchors;
    // Compiled counterpart to followingAnchors. It avoids reparsing anchor
    // text and preserves the capture-to-anchor relationship for adjacent
    // captures in leading and trailing mixfix forms.
    std::vector<std::optional<std::size_t>> followingAnchorIndices;
    OperatorFixity fixity = OperatorFixity::Infix;
    bool startsWithCapture = true;
    bool hasDeclaredFixity = false;
    // @mixfix lets the pattern shape choose infix/prefix/postfix/mixfix.
    bool inferFixityFromPattern = false;
    // Only @mixfix declarations satisfy a `mixfix` capture.  This is kept on
    // the canonical pattern so structural overload matching stays registry based.
    bool isMixfixDeclaration = false;
    OperatorPrecedence precedence = OperatorPrecedence::Relationship;
    OperatorAssociativity associativity = OperatorAssociativity::None;
    OperatorVisibility visibility = OperatorVisibility::Private;
    std::string module;
};

struct OperatorPatternOrigin {
    std::string module;
    OperatorVisibility visibility = OperatorVisibility::Private;
};

struct OperatorOverloadDefinition {
    OperatorId operatorId = 0;
    PatternId patternId = 0;
    SymbolId methodId = 0;
    std::string methodName;
    std::vector<OperatorTypeBinding> captures;
    std::vector<OperatorTypeBinding> factors;
    std::string resultType;
    SymbolId resultTypeId = 0;
    LanguageTypeId resultLanguageTypeId = LanguageTypeId::Unknown;
    OperatorCardinality cardinality = OperatorCardinality::One;
    OperatorEffect effect = OperatorEffect::Pure;
    OperatorVisibility visibility = OperatorVisibility::Private;
    std::string module;
};

struct OperatorMatcherDefinition {
    OperatorId operatorId = 0;
    PatternId patternId = 0;
    SymbolId methodId = 0;
    std::string methodName;
    std::vector<OperatorTypeBinding> captures;
    std::vector<OperatorTypeBinding> produces;
    OperatorVisibility visibility = OperatorVisibility::Private;
    std::string module;
};

class OperatorRegistry {
public:
    const OperatorPatternDefinition& registerPattern(OperatorPatternDefinition pattern) {
        if (pattern.anchorLexemes.empty() && !pattern.pattern.empty()) {
            compilePattern(pattern);
        }
        const CoreOperator core = coreOperatorForPattern(pattern.pattern);
        if (core != CoreOperator::Unknown) {
            const auto protectedSpelling = protectedCoreSpelling(core);
            if (!protectedSpelling.empty()) {
                throw std::runtime_error(
                    "Operator '" + std::string(protectedSpelling) + "' is protected and cannot be overloaded");
            }
            pattern.operatorId = static_cast<OperatorId>(core);
            pattern.patternId = static_cast<PatternId>(core);
        }
        const std::string key = pattern.operatorName + "\n" + pattern.pattern;
        auto found = patternByContract_.find(key);
        if (found != patternByContract_.end()) {
            const auto& current = patterns_.at(found->second);
            if (current.precedence != pattern.precedence ||
                current.associativity != pattern.associativity ||
                current.fixity != pattern.fixity) {
                throw std::runtime_error("Operator pattern cannot redefine precedence, associativity, or type");
            }
            registerOrigin(found->second, pattern.module, pattern.visibility);
            return current;
        }
        if (pattern.operatorId == 0) pattern.operatorId = nextOperatorId_++;
        if (pattern.patternId == 0) pattern.patternId = nextPatternId_++;
        const std::size_t index = patterns_.size();
        patternByContract_.emplace(key, index);
        patterns_.push_back(std::move(pattern));
        patternById_.emplace(patterns_.back().patternId, index);
        registerOrigin(index, patterns_.back().module, patterns_.back().visibility);
        const auto& stored = patterns_.back();
        if (!stored.anchorLexemes.empty() && !stored.anchorLexemes.front().empty()) {
            const auto& first = stored.anchorLexemes.front().front();
            patternsByFirstPieceSequence_[first.pieceIds].push_back(index);
        }
        return stored;
    }

    void registerOverload(OperatorOverloadDefinition overload) {
        rejectDuplicateTypes(overload.factors, "factor");
        rejectVisibilityAbovePattern(
            overload.patternId, overload.module, overload.visibility, "overload");
        for (const auto& matcher : matchers_) {
            if (matcher.patternId != overload.patternId ||
                !sameTypeSignature(matcher.produces, overload.factors)) continue;
            if (visibilityRank(matcher.visibility) > visibilityRank(overload.visibility)) {
                throw std::runtime_error(
                    "@matcher visibility cannot exceed the factored overload visibility");
            }
        }
        overloadsByPattern_[overload.patternId].push_back(overloads_.size());
        overloads_.push_back(std::move(overload));
    }

    void registerMatcher(OperatorMatcherDefinition matcher) {
        rejectDuplicateTypes(matcher.produces, "produced Requirement");
        rejectVisibilityAbovePattern(
            matcher.patternId, matcher.module, matcher.visibility, "matcher");
        for (const auto& overload : overloads_) {
            if (overload.patternId != matcher.patternId ||
                !sameTypeSignature(matcher.produces, overload.factors)) continue;
            if (visibilityRank(matcher.visibility) > visibilityRank(overload.visibility)) {
                throw std::runtime_error(
                    "@matcher visibility cannot exceed the factored overload visibility");
            }
        }
        matchersByPatternAndArity_[matcher.patternId][matcher.produces.size()].push_back(
            matchers_.size());
        matchers_.push_back(std::move(matcher));
    }

    std::vector<const OperatorPatternDefinition*> patternsForAnchor(const std::vector<int>& pieceIds,
                                                                    std::string_view module = {}) const {
        std::vector<const OperatorPatternDefinition*> matches;
        auto found = patternsByFirstPieceSequence_.find(pieceIds);
        if (found == patternsByFirstPieceSequence_.end()) return matches;
        matches.reserve(found->second.size());
        for (const auto index : found->second) {
            const auto& pattern = patterns_[index];
            const auto origins = patternOrigins_.find(index);
            if (origins == patternOrigins_.end()) continue;
            const bool local = std::any_of(
                origins->second.begin(), origins->second.end(), [&](const auto& origin) {
                    return origin.module == module;
                });
            if (local) {
                matches.push_back(&pattern);
                continue;
            }
            const auto publicCount = static_cast<std::size_t>(std::count_if(
                origins->second.begin(), origins->second.end(), [](const auto& origin) {
                    return origin.visibility == OperatorVisibility::Public;
                }));
            // Repeating the canonical pattern lets the parser issue its normal
            // ambiguity diagnostic without allowing import order to pick one.
            for (std::size_t i = 0; i < publicCount; ++i) matches.push_back(&pattern);
        }
        return matches;
    }

    std::vector<const OperatorPatternDefinition*> patternsForAnchor(
        const Token& token, std::string_view module = {}) const {
        return patternsForAnchor(token.pieceIds, module);
    }

    std::vector<const OperatorPatternDefinition*> leadingPatternsForAnchor(
        const Token& token, std::string_view module = {}) const {
        auto matches = patternsForAnchor(token, module);
        matches.erase(std::remove_if(matches.begin(), matches.end(), [](const auto* pattern) {
            return pattern->startsWithCapture;
        }), matches.end());
        return matches;
    }

    std::vector<const OperatorPatternDefinition*> trailingPatternsForAnchor(
        const Token& token, std::string_view module = {}) const {
        auto matches = patternsForAnchor(token, module);
        matches.erase(std::remove_if(matches.begin(), matches.end(), [](const auto* pattern) {
            return pattern->fixity == OperatorFixity::Prefix;
        }), matches.end());
        return matches;
    }

    std::vector<const OperatorPatternDefinition*> deferredTrailingCapturePatterns(
        std::string_view module = {}) const {
        std::vector<const OperatorPatternDefinition*> matches;
        for (std::size_t index = 0; index < patterns_.size(); ++index) {
            const auto& pattern = patterns_[index];
            if (!pattern.startsWithCapture || pattern.captureNames.size() < 2) continue;
            const bool hasAnchorAfterFirstCapture = std::any_of(
                pattern.followingAnchorIndices.begin() + 1,
                pattern.followingAnchorIndices.end(),
                [](const auto& anchor) { return anchor.has_value(); });
            if (!hasAnchorAfterFirstCapture) continue;
            const auto origins = patternOrigins_.find(index);
            if (origins == patternOrigins_.end()) continue;
            const bool local = std::any_of(
                origins->second.begin(), origins->second.end(), [&](const auto& origin) {
                    return origin.module == module;
                });
            if (local) {
                matches.push_back(&pattern);
                continue;
            }
            const auto publicCount = static_cast<std::size_t>(std::count_if(
                origins->second.begin(), origins->second.end(), [](const auto& origin) {
                    return origin.visibility == OperatorVisibility::Public;
                }));
            for (std::size_t count = 0; count < publicCount; ++count) {
                matches.push_back(&pattern);
            }
        }
        return matches;
    }

    const std::vector<OperatorPatternDefinition>& patterns() const { return patterns_; }
    const std::vector<OperatorOverloadDefinition>& overloads() const { return overloads_; }
    const std::vector<OperatorMatcherDefinition>& matchers() const { return matchers_; }
    std::vector<const OperatorOverloadDefinition*> overloadsForPattern(PatternId patternId) const {
        std::vector<const OperatorOverloadDefinition*> matches;
        const auto found = overloadsByPattern_.find(patternId);
        if (found == overloadsByPattern_.end()) return matches;
        matches.reserve(found->second.size());
        for (const auto index : found->second) matches.push_back(&overloads_.at(index));
        return matches;
    }
    std::vector<const OperatorMatcherDefinition*> matchersForPattern(
        PatternId patternId,
        std::size_t producedCount) const {
        std::vector<const OperatorMatcherDefinition*> matches;
        const auto pattern = matchersByPatternAndArity_.find(patternId);
        if (pattern == matchersByPatternAndArity_.end()) return matches;
        const auto found = pattern->second.find(producedCount);
        if (found == pattern->second.end()) return matches;
        matches.reserve(found->second.size());
        for (const auto index : found->second) matches.push_back(&matchers_.at(index));
        return matches;
    }
    const OperatorPatternDefinition* findPatternById(PatternId patternId) const {
        const auto found = patternById_.find(patternId);
        return found == patternById_.end() ? nullptr : &patterns_.at(found->second);
    }
    const OperatorPatternDefinition* findPattern(std::string_view operatorName,
                                                 std::string_view pattern) const {
        const std::string key = std::string(operatorName) + "\n" + std::string(pattern);
        auto found = patternByContract_.find(key);
        return found == patternByContract_.end() ? nullptr : &patterns_[found->second];
    }
    const OperatorPatternDefinition* findPatternByOperator(std::string_view operatorName) const {
        const OperatorPatternDefinition* match = nullptr;
        for (const auto& pattern : patterns_) {
            if (pattern.operatorName != operatorName) continue;
            if (match && match->patternId != pattern.patternId) {
                throw std::runtime_error("Operator name has multiple patterns; specify 'pattern'");
            }
            match = &pattern;
        }
        return match;
    }
    bool hasVisiblePattern(PatternId patternId, std::string_view module) const {
        for (std::size_t index = 0; index < patterns_.size(); ++index) {
            if (patterns_[index].patternId != patternId) continue;
            const auto found = patternOrigins_.find(index);
            if (found == patternOrigins_.end()) return false;
            return std::any_of(found->second.begin(), found->second.end(), [&](const auto& origin) {
                return origin.module == module || origin.visibility == OperatorVisibility::Public;
            });
        }
        return false;
    }

    OperatorVisibility visibilityForPattern(PatternId patternId,
                                            std::string_view module) const {
        for (std::size_t index = 0; index < patterns_.size(); ++index) {
            if (patterns_[index].patternId != patternId) continue;
            const auto found = patternOrigins_.find(index);
            if (found == patternOrigins_.end()) return OperatorVisibility::Private;
            for (const auto& origin : found->second) {
                if (origin.module == module) return origin.visibility;
            }
            const auto publicCount = std::count_if(
                found->second.begin(), found->second.end(), [](const auto& origin) {
                    return origin.visibility == OperatorVisibility::Public;
                });
            if (publicCount == 1) return OperatorVisibility::Public;
            if (publicCount > 1) throw std::runtime_error(
                "Conflicting imported public operator syntax");
            return OperatorVisibility::Private;
        }
        return OperatorVisibility::Private;
    }

public:
    // Structural compilation is intentionally lexical-model agnostic. Parser
    // attaches its native SentencePiece IDs exactly once before registration.
    static CoreOperator coreOperatorForPattern(std::string_view pattern) {
        if (pattern == "{left} + {right}") return CoreOperator::Add;
        if (pattern == "{left} - {right}") return CoreOperator::Subtract;
        if (pattern == "{left} * {right}") return CoreOperator::Multiply;
        if (pattern == "{left} / {right}") return CoreOperator::Divide;
        if (pattern == "{left} % {right}") return CoreOperator::Modulo;
        if (pattern == "{left} < {right}") return CoreOperator::Less;
        if (pattern == "{left} <= {right}") return CoreOperator::LessEqual;
        if (pattern == "{left} > {right}") return CoreOperator::Greater;
        if (pattern == "{left} >= {right}") return CoreOperator::GreaterEqual;
        if (pattern == "{left} == {right}") return CoreOperator::StrictEqual;
        if (pattern == "{left} != {right}") return CoreOperator::StrictNotEqual;
        if (pattern == "{left} then {right}") return CoreOperator::Then;
        if (pattern == "{left} and {right}") return CoreOperator::LogicalAnd;
        if (pattern == "{left} or {right}") return CoreOperator::LogicalOr;
        if (pattern == "not {value}") return CoreOperator::LogicalNot;
        return CoreOperator::Unknown;
    }

    static std::string_view protectedCoreSpelling(CoreOperator core) {
        switch (core) {
            case CoreOperator::StrictEqual: return "==";
            case CoreOperator::StrictNotEqual: return "!=";
            case CoreOperator::Then: return "then";
            case CoreOperator::LogicalAnd: return "and";
            case CoreOperator::LogicalOr: return "or";
            case CoreOperator::LogicalNot: return "not";
            default: return {};
        }
    }

    void registerOrigin(std::size_t patternIndex,
                        const std::string& module,
                        OperatorVisibility visibility) {
        auto& origins = patternOrigins_[patternIndex];
        for (const auto& origin : origins) {
            if (origin.module != module) continue;
            if (origin.visibility != visibility) {
                throw std::runtime_error(
                    "Operator pattern cannot redefine visibility in the same module");
            }
            return;
        }
        origins.push_back(OperatorPatternOrigin{module, visibility});
    }

    static void compilePattern(OperatorPatternDefinition& pattern) {
        pattern.operatorNameId = symbolIdForName(pattern.operatorName);
        std::size_t cursor = 0;
        bool sawSegment = false;
        bool lastWasCapture = false;
        while (cursor < pattern.pattern.size()) {
            while (cursor < pattern.pattern.size() && pattern.pattern[cursor] == ' ') ++cursor;
            if (cursor >= pattern.pattern.size()) break;
            const bool capture = pattern.pattern[cursor] == '{';
            if (!sawSegment) pattern.startsWithCapture = capture;
            if (capture) {
                const auto close = pattern.pattern.find('}', cursor + 1);
                if (close == std::string::npos) throw std::runtime_error("Unclosed operator pattern capture");
                const auto name = pattern.pattern.substr(cursor + 1, close - cursor - 1);
                if (name.empty()) throw std::runtime_error("Operator pattern capture cannot be empty");
                if (name.find_first_of(" {}\t\r\n") != std::string::npos) {
                    throw std::runtime_error("Operator pattern capture must be a single binding name");
                }
                pattern.captureNames.push_back(name);
                pattern.followingAnchors.emplace_back();
                pattern.followingAnchorIndices.emplace_back(std::nullopt);
                cursor = close + 1;
            } else {
                const auto next = pattern.pattern.find('{', cursor);
                std::size_t end = next == std::string::npos ? pattern.pattern.size() : next;
                while (end > cursor && pattern.pattern[end - 1] == ' ') --end;
                const auto anchor = pattern.pattern.substr(cursor, end - cursor);
                if (anchor.empty()) throw std::runtime_error("Operator pattern anchor cannot be empty");
                pattern.anchors.push_back(anchor);
                if (lastWasCapture && !pattern.followingAnchors.empty()) {
                    pattern.followingAnchors.back() = anchor;
                    pattern.followingAnchorIndices.back() = pattern.anchors.size() - 1;
                }
                cursor = next == std::string::npos ? pattern.pattern.size() : next;
            }
            lastWasCapture = capture;
            sawSegment = true;
        }
        if (!sawSegment || pattern.anchors.empty()) {
            throw std::runtime_error("Operator pattern requires at least one literal anchor");
        }
        pattern.anchorLexemes.clear();
        pattern.anchorLexemes.reserve(pattern.anchors.size());
        for (const auto& anchor : pattern.anchors) {
            std::vector<PatternLexeme> lexemes;
            std::size_t wordStart = 0;
            while (wordStart < anchor.size()) {
                while (wordStart < anchor.size() &&
                       (anchor[wordStart] == ' ' || anchor[wordStart] == '\t')) {
                    ++wordStart;
                }
                if (wordStart >= anchor.size()) break;
                const auto wordEnd = anchor.find_first_of(" \t", wordStart);
                const std::string word = anchor.substr(
                    wordStart,
                    wordEnd == std::string::npos ? std::string::npos : wordEnd - wordStart);
                lexemes.push_back(PatternLexeme{word, symbolIdForName(word), {}});
                wordStart = wordEnd == std::string::npos ? anchor.size() : wordEnd + 1;
            }
            pattern.anchorLexemes.push_back(std::move(lexemes));
        }
        const bool endsWithCapture = lastWasCapture;
        OperatorFixity inferredFixity = OperatorFixity::Infix;
        if (!pattern.startsWithCapture && endsWithCapture) {
            inferredFixity = pattern.captureNames.size() == 1 && pattern.anchors.size() == 1
                ? OperatorFixity::Prefix : OperatorFixity::Mixfix;
        } else if (pattern.startsWithCapture && !endsWithCapture) {
            inferredFixity = pattern.captureNames.size() == 1 && pattern.anchors.size() == 1
                ? OperatorFixity::Postfix : OperatorFixity::Mixfix;
        } else if (pattern.startsWithCapture && endsWithCapture) {
            inferredFixity = pattern.captureNames.size() == 2 && pattern.anchors.size() == 1
                ? OperatorFixity::Infix : OperatorFixity::Mixfix;
        } else {
            inferredFixity = OperatorFixity::Mixfix;
        }
        if (pattern.hasDeclaredFixity && pattern.fixity != inferredFixity) {
            throw std::runtime_error("Operator pattern shape does not match its declared type");
        }
        if (!pattern.hasDeclaredFixity && !pattern.inferFixityFromPattern &&
            inferredFixity != OperatorFixity::Infix) {
            throw std::runtime_error(
                "Non-infix operator patterns require type: prefix, postfix, or mixfix");
        }
        pattern.fixity = inferredFixity;
    }

    static void rejectDuplicateTypes(const std::vector<OperatorTypeBinding>& bindings,
                                     const char* label) {
        std::unordered_map<SymbolId, std::string> seen;
        for (const auto& binding : bindings) {
            if (!seen.emplace(binding.typeId, binding.type).second) {
                throw std::runtime_error(std::string("Duplicate ") + label + " type '" + binding.type + "'");
            }
        }
    }

    static int visibilityRank(OperatorVisibility visibility) {
        return visibility == OperatorVisibility::Public ? 1 : 0;
    }

    static bool sameTypeSignature(const std::vector<OperatorTypeBinding>& left,
                                  const std::vector<OperatorTypeBinding>& right) {
        if (left.size() != right.size()) return false;
        std::vector<SymbolId> leftTypes;
        std::vector<SymbolId> rightTypes;
        leftTypes.reserve(left.size());
        rightTypes.reserve(right.size());
        for (const auto& binding : left) leftTypes.push_back(binding.typeId);
        for (const auto& binding : right) rightTypes.push_back(binding.typeId);
        std::sort(leftTypes.begin(), leftTypes.end());
        std::sort(rightTypes.begin(), rightTypes.end());
        return leftTypes == rightTypes;
    }

    void rejectVisibilityAbovePattern(PatternId patternId,
                                      const std::string& module,
                                      OperatorVisibility visibility,
                                      const char* declaration) const {
        if (visibility != OperatorVisibility::Public) return;
        if (visibilityForPattern(patternId, module) == OperatorVisibility::Private) {
            throw std::runtime_error(
                std::string("Public operator ") + declaration +
                " requires public operator syntax");
        }
    }

    OperatorId nextOperatorId_ = 1024;
    PatternId nextPatternId_ = 1024;
    std::vector<OperatorPatternDefinition> patterns_;
    std::vector<OperatorOverloadDefinition> overloads_;
    std::vector<OperatorMatcherDefinition> matchers_;
    std::unordered_map<std::string, std::size_t> patternByContract_;
    std::unordered_map<PatternId, std::size_t> patternById_;
    std::unordered_map<std::vector<int>, std::vector<std::size_t>, PieceSequenceHash>
        patternsByFirstPieceSequence_;
    std::unordered_map<PatternId, std::vector<std::size_t>> overloadsByPattern_;
    std::unordered_map<PatternId,
        std::unordered_map<std::size_t, std::vector<std::size_t>>>
        matchersByPatternAndArity_;
    std::unordered_map<std::size_t, std::vector<OperatorPatternOrigin>> patternOrigins_;
};

struct CoreOperatorDefinition {
    CoreOperator id = CoreOperator::Unknown;
    PatternId patternId = 0;
    TokenType token = TokenType::End;
    std::string_view spelling;
    OperatorPrecedence precedence = OperatorPrecedence::Relationship;
    OperatorAssociativity associativity = OperatorAssociativity::None;
    OperatorFixity fixity = OperatorFixity::Infix;
    bool overloadable = false;
};

inline constexpr PatternId corePatternId(CoreOperator id) {
    return static_cast<PatternId>(id);
}

inline constexpr OperatorId operatorId(CoreOperator id) {
    return static_cast<OperatorId>(id);
}

inline constexpr CoreOperatorDefinition coreOperatorDefinition(CoreOperator id) {
    switch (id) {
        case CoreOperator::Add:
            return {id, corePatternId(id), TokenType::Plus, "+", OperatorPrecedence::Additive,
                    OperatorAssociativity::Left, OperatorFixity::Infix, true};
        case CoreOperator::Subtract:
            return {id, corePatternId(id), TokenType::Minus, "-", OperatorPrecedence::Additive,
                    OperatorAssociativity::Left, OperatorFixity::Infix, true};
        case CoreOperator::Multiply:
            return {id, corePatternId(id), TokenType::Star, "*", OperatorPrecedence::Multiplicative,
                    OperatorAssociativity::Left, OperatorFixity::Infix, true};
        case CoreOperator::Divide:
            return {id, corePatternId(id), TokenType::Slash, "/", OperatorPrecedence::Multiplicative,
                    OperatorAssociativity::Left, OperatorFixity::Infix, true};
        case CoreOperator::Modulo:
            return {id, corePatternId(id), TokenType::Percent, "%", OperatorPrecedence::Multiplicative,
                    OperatorAssociativity::Left, OperatorFixity::Infix, true};
        case CoreOperator::Less:
            return {id, corePatternId(id), TokenType::LT, "<", OperatorPrecedence::Ordering,
                    OperatorAssociativity::None, OperatorFixity::Infix, true};
        case CoreOperator::LessEqual:
            return {id, corePatternId(id), TokenType::LTE, "<=", OperatorPrecedence::Ordering,
                    OperatorAssociativity::None, OperatorFixity::Infix, true};
        case CoreOperator::Greater:
            return {id, corePatternId(id), TokenType::GT, ">", OperatorPrecedence::Ordering,
                    OperatorAssociativity::None, OperatorFixity::Infix, true};
        case CoreOperator::GreaterEqual:
            return {id, corePatternId(id), TokenType::GTE, ">=", OperatorPrecedence::Ordering,
                    OperatorAssociativity::None, OperatorFixity::Infix, true};
        case CoreOperator::StrictEqual:
            return {id, corePatternId(id), TokenType::EqEq, "==", OperatorPrecedence::Ordering,
                    OperatorAssociativity::None, OperatorFixity::Infix, false};
        case CoreOperator::StrictNotEqual:
            return {id, corePatternId(id), TokenType::NotEq, "!=", OperatorPrecedence::Ordering,
                    OperatorAssociativity::None, OperatorFixity::Infix, false};
        case CoreOperator::Then:
            return {id, corePatternId(id), TokenType::Then, "then", OperatorPrecedence::Pipeline,
                    OperatorAssociativity::Left, OperatorFixity::Infix, false};
        case CoreOperator::UnaryPlus:
            return {id, corePatternId(id), TokenType::Plus, "+", OperatorPrecedence::Prefix,
                    OperatorAssociativity::Right, OperatorFixity::Prefix, false};
        case CoreOperator::UnaryMinus:
            return {id, corePatternId(id), TokenType::Minus, "-", OperatorPrecedence::Prefix,
                    OperatorAssociativity::Right, OperatorFixity::Prefix, false};
        case CoreOperator::LogicalAnd:
            return {id, corePatternId(id), TokenType::And, "and", OperatorPrecedence::LogicalAnd,
                    OperatorAssociativity::Left, OperatorFixity::Infix, false};
        case CoreOperator::LogicalOr:
            return {id, corePatternId(id), TokenType::Or, "or", OperatorPrecedence::LogicalOr,
                    OperatorAssociativity::Left, OperatorFixity::Infix, false};
        case CoreOperator::LogicalNot:
            return {id, corePatternId(id), TokenType::Not, "not", OperatorPrecedence::Prefix,
                    OperatorAssociativity::Right, OperatorFixity::Prefix, false};
        case CoreOperator::Unknown:
            break;
    }
    return {};
}

inline constexpr std::optional<CoreOperatorDefinition> infixOperatorForId(TokenId::Id id) {
    switch (id) {
        case TokenId::PLUS: return coreOperatorDefinition(CoreOperator::Add);
        case TokenId::MINUS: return coreOperatorDefinition(CoreOperator::Subtract);
        case TokenId::STAR: return coreOperatorDefinition(CoreOperator::Multiply);
        case TokenId::SLASH: return coreOperatorDefinition(CoreOperator::Divide);
        case TokenId::PERCENT: return coreOperatorDefinition(CoreOperator::Modulo);
        case TokenId::LESS: return coreOperatorDefinition(CoreOperator::Less);
        case TokenId::LESS_EQUAL: return coreOperatorDefinition(CoreOperator::LessEqual);
        case TokenId::GREATER: return coreOperatorDefinition(CoreOperator::Greater);
        case TokenId::GREATER_EQUAL: return coreOperatorDefinition(CoreOperator::GreaterEqual);
        case TokenId::EQUAL: return coreOperatorDefinition(CoreOperator::StrictEqual);
        case TokenId::NOT_EQUAL: return coreOperatorDefinition(CoreOperator::StrictNotEqual);
        case TokenId::AND: return coreOperatorDefinition(CoreOperator::LogicalAnd);
        case TokenId::OR: return coreOperatorDefinition(CoreOperator::LogicalOr);
        case TokenId::THEN: return coreOperatorDefinition(CoreOperator::Then);
        default: return std::nullopt;
    }
}

inline constexpr bool isComparisonOperator(CoreOperator id) {
    return id == CoreOperator::Less || id == CoreOperator::LessEqual ||
           id == CoreOperator::Greater || id == CoreOperator::GreaterEqual ||
           id == CoreOperator::StrictEqual || id == CoreOperator::StrictNotEqual;
}

} // namespace Felidae
