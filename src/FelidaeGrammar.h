#pragma once

#include "Symbol.h"
#include "form/BuiltinOperation.h"
#ifndef FELIDAE_GENERATING_MODEL
#include <FelidaeSentencePieceIds.h>
#endif
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Felidae {

inline constexpr bool isCustomOperatorCharacter(char value) {
  switch (value) {
  case '~':
  case '^':
  case '&':
  case '$':
    return true;
  default:
    return false;
  }
}

inline bool isCustomOperatorSpelling(std::string_view spelling) {
  return !spelling.empty() &&
         std::all_of(spelling.begin(), spelling.end(), [](char value) {
           return isCustomOperatorCharacter(value);
         });
}

enum class LanguageTypeId {
  Unknown = 0,
  Any,
  Array,
  Bool,
  Boolean,
  Decimal,
  Double,
  Fact,
  Float,
  Int,
  Number,
  String,
  Expr,
  Mixfix,
  Stmt,
  Statements
};

inline constexpr std::string_view languageTypeName(LanguageTypeId type) {
  switch (type) {
  case LanguageTypeId::Any:
    return "any";
  case LanguageTypeId::Array:
    return "array";
  case LanguageTypeId::Bool:
    return "bool";
  case LanguageTypeId::Boolean:
    return "boolean";
  case LanguageTypeId::Decimal:
    return "decimal";
  case LanguageTypeId::Double:
    return "double";
  case LanguageTypeId::Fact:
    return "Fact";
  case LanguageTypeId::Float:
    return "float";
  case LanguageTypeId::Int:
    return "int";
  case LanguageTypeId::Number:
    return "number";
  case LanguageTypeId::String:
    return "string";
  case LanguageTypeId::Expr:
    return "expr";
  case LanguageTypeId::Mixfix:
    return "mixfix";
  case LanguageTypeId::Stmt:
    return "stmt";
  case LanguageTypeId::Statements:
    return "stmts";
  case LanguageTypeId::Unknown:
    return {};
  }
  return {};
}

inline LanguageTypeId languageTypeIdForName(const std::string &name) {
  // `obj` is the concise generic-capture spelling used by existing Felidae
  // mixfix declarations. It is an alias, not a distinct runtime type: both
  // spellings retain Any's intentionally unconstrained matching semantics.
  if (name == "any" || name == "obj")
    return LanguageTypeId::Any;
  if (name == "array")
    return LanguageTypeId::Array;
  if (name == "bool")
    return LanguageTypeId::Bool;
  if (name == "boolean")
    return LanguageTypeId::Boolean;
  if (name == "decimal")
    return LanguageTypeId::Decimal;
  if (name == "double")
    return LanguageTypeId::Double;
  if (name == "Fact")
    return LanguageTypeId::Fact;
  if (name == "float")
    return LanguageTypeId::Float;
  if (name == "int")
    return LanguageTypeId::Int;
  if (name == "number")
    return LanguageTypeId::Number;
  if (name == "string")
    return LanguageTypeId::String;
  if (name == "expr")
    return LanguageTypeId::Expr;
  if (name == "mixfix")
    return LanguageTypeId::Mixfix;
  if (name == "stmt")
    return LanguageTypeId::Stmt;
  if (name == "stmts")
    return LanguageTypeId::Statements;
  return LanguageTypeId::Unknown;
}

inline bool isFelidaeBuiltinTypeName(const std::string &name) {
  return languageTypeIdForName(name) != LanguageTypeId::Unknown;
}

inline bool isFelidaeTypeAnnotationName(const std::string &name) {
  return !name.empty() &&
         (std::isupper(static_cast<unsigned char>(name.front())) ||
          isFelidaeBuiltinTypeName(name));
}

inline bool isFelidaeLikelyTypeName(const std::string &name) {
  return isFelidaeBuiltinTypeName(name) ||
         (!name.empty() &&
          std::isupper(static_cast<unsigned char>(name.front())));
}

// Model-generation spelling specification for fixed Felidae syntax. Runtime
// grammar IDs are generated into FelidaeSentencePieceIds.h; this file never
// owns numeric token identities.
struct BuiltinTokenDefinition {
  std::string_view spelling;
  std::string_view idName;
};

inline constexpr BuiltinTokenDefinition kBuiltinTokens[] = {
    {"import", "IMPORT"},
    {"not", "NOT"},
    {"and", "AND"},
    {"or", "OR"},
    {"then", "THEN"},
    {"as", "AS"},
    {"if", "IF"},
    {"else", "ELSE"},
    {"return", "RETURN"},
    {"where", "WHERE"},
    {"extend", "EXTEND"},
    {"lambda", "LAMBDA"},
    {"true", "TRUE"},
    {"false", "FALSE"},
    {"nil", "NIL"},
    {"(", "LPAREN"},
    {")", "RPAREN"},
    {"{", "LBRACE"},
    {"}", "RBRACE"},
    {"[", "LBRACKET"},
    {"]", "RBRACKET"},
    {",", "COMMA"},
    {":", "COLON"},
    {".", "DOT"},
    {"|", "PIPE"},
    {"?", "QUESTION"},
    {"@", "AT"},
    {":=", "ASSIGN"},
    {"::", "DOUBLE_COLON"},
    {"=>", "ARROW"},
    {"+", "PLUS"},
    {"-", "MINUS"},
    {"*", "STAR"},
    {"/", "SLASH"},
    {"%", "PERCENT"},
    {"==", "EQUAL"},
    {"!=", "NOT_EQUAL"},
    {"<", "LESS"},
    {"<=", "LESS_EQUAL"},
    {">", "GREATER"},
    {">=", "GREATER_EQUAL"},
    // Literal and trivia delimiters are grammar IDs too.  The integer parser
    // must not rediscover them by inspecting source characters after model
    // encoding.
    {"\"", "QUOTE"},
    {"\\", "BACKSLASH"},
    {"#", "COMMENT"},
    {"▁", "SPACE"},
    {"\t", "TAB"},
    {"\n", "NEWLINE"},
    {"\r", "CARRIAGE_RETURN"},
    {"0", "DIGIT_0"},
    {"1", "DIGIT_1"},
    {"2", "DIGIT_2"},
    {"3", "DIGIT_3"},
    {"4", "DIGIT_4"},
    {"5", "DIGIT_5"},
    {"6", "DIGIT_6"},
    {"7", "DIGIT_7"},
    {"8", "DIGIT_8"},
    {"9", "DIGIT_9"},
};

#ifndef FELIDAE_GENERATING_MODEL
static_assert(
    std::size(kBuiltinTokens) == std::size(kFelidaeBuiltinSentencePieceIds),
    "Regenerate FelidaeSentencePieceIds.h after changing built-in syntax");
#endif

inline constexpr const BuiltinTokenDefinition *
builtinTokenForSpelling(std::string_view spelling) {
  for (const auto &token : kBuiltinTokens) {
    if (token.spelling == spelling)
      return &token;
  }
  return nullptr;
}

// Integer grammar vocabulary query.  The parser uses this directly when it
// assembles non-built-in SentencePiece ranges; no lexer classification is
// involved.
#ifndef FELIDAE_GENERATING_MODEL
inline constexpr bool isBuiltinTokenId(TokenId::Id id) {
  for (const auto builtin : kFelidaeBuiltinSentencePieceIds) {
    if (id == builtin)
      return true;
  }
  return false;
}

inline constexpr std::string_view builtinTokenSpelling(TokenId::Id id) {
  for (std::size_t index = 0; index < std::size(kBuiltinTokens); ++index) {
    if (kFelidaeBuiltinSentencePieceIds[index] == id)
      return kBuiltinTokens[index].spelling;
  }
  return {};
}

// These IDs end an already-started identifier range.  Keyword IDs are
// intentionally absent: if SentencePiece emits (for example) the `or` ID in
// the middle of `Record`, the contiguous IDs still represent one identifier.
// At the start of a grammar position, atNameRange() rejects every built-in ID
// so reserved words continue to be parsed as grammar vocabulary.
inline constexpr bool isIdentifierBoundaryId(TokenId::Id id) {
  switch (id) {
  case TokenId::LPAREN:
  case TokenId::RPAREN:
  case TokenId::LBRACE:
  case TokenId::RBRACE:
  case TokenId::LBRACKET:
  case TokenId::RBRACKET:
  case TokenId::COMMA:
  case TokenId::COLON:
  case TokenId::DOT:
  case TokenId::PIPE:
  case TokenId::QUESTION:
  case TokenId::AT:
  case TokenId::ASSIGN:
  case TokenId::DOUBLE_COLON:
  case TokenId::ARROW:
  case TokenId::PLUS:
  case TokenId::MINUS:
  case TokenId::STAR:
  case TokenId::SLASH:
  case TokenId::PERCENT:
  case TokenId::EQUAL:
  case TokenId::NOT_EQUAL:
  case TokenId::LESS:
  case TokenId::LESS_EQUAL:
  case TokenId::GREATER:
  case TokenId::GREATER_EQUAL:
  case TokenId::QUOTE:
  case TokenId::BACKSLASH:
  case TokenId::COMMENT:
  case TokenId::SPACE:
  case TokenId::TAB:
  case TokenId::NEWLINE:
  case TokenId::CARRIAGE_RETURN:
    return true;
  default:
    return false;
  }
}

inline constexpr bool isDecimalDigitId(TokenId::Id id) {
  return id >= TokenId::DIGIT_0 && id <= TokenId::DIGIT_9;
}
#endif

} // namespace Felidae
