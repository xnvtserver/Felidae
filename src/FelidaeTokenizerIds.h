// Fixed grammar syntax IDs.
#pragma once

#include <cstdint>

namespace Felidae {
namespace TokenId {
using Id = std::int32_t;
constexpr Id UNKNOWN = 0;
constexpr Id IMPORT = 1;
constexpr Id NOT = 2;
constexpr Id AND = 3;
constexpr Id OR = 4;
constexpr Id THEN = 5;
constexpr Id AS = 6;
constexpr Id IF = 7;
constexpr Id ELSE = 8;
constexpr Id RETURN = 9;
constexpr Id WHERE = 10;
constexpr Id EXTEND = 11;
constexpr Id LAMBDA = 12;
constexpr Id TRUE = 13;
constexpr Id FALSE = 14;
constexpr Id NIL = 15;
constexpr Id LPAREN = 16;
constexpr Id RPAREN = 17;
constexpr Id LBRACE = 18;
constexpr Id RBRACE = 19;
constexpr Id LBRACKET = 20;
constexpr Id RBRACKET = 21;
constexpr Id COMMA = 22;
constexpr Id COLON = 23;
constexpr Id DOT = 24;
constexpr Id PIPE = 25;
constexpr Id QUESTION = 26;
constexpr Id AT = 27;
constexpr Id ASSIGN = 28;
constexpr Id DOUBLE_COLON = 29;
constexpr Id ARROW = 30;
constexpr Id PLUS = 31;
constexpr Id MINUS = 32;
constexpr Id STAR = 33;
constexpr Id SLASH = 34;
constexpr Id PERCENT = 35;
constexpr Id EQUAL = 36;
constexpr Id NOT_EQUAL = 37;
constexpr Id LESS = 38;
constexpr Id LESS_EQUAL = 39;
constexpr Id GREATER = 40;
constexpr Id GREATER_EQUAL = 41;
constexpr Id QUOTE = 42;
constexpr Id BACKSLASH = 43;
constexpr Id COMMENT = 44;
constexpr Id SPACE = 45;
constexpr Id TAB = 46;
constexpr Id NEWLINE = 47;
constexpr Id CARRIAGE_RETURN = 48;
constexpr Id DIGIT_0 = 49;
constexpr Id DIGIT_1 = 50;
constexpr Id DIGIT_2 = 51;
constexpr Id DIGIT_3 = 52;
constexpr Id DIGIT_4 = 53;
constexpr Id DIGIT_5 = 54;
constexpr Id DIGIT_6 = 55;
constexpr Id DIGIT_7 = 56;
constexpr Id DIGIT_8 = 57;
constexpr Id DIGIT_9 = 58;
// Lexer-owned reserved words deliberately sit outside the fixed grammar IDs
// above (1..58) and the byte-token range (WordVocabulary::kFirstByteToken
// and up, src/Tokenizer.h) - negative IDs can never collide with either.
constexpr Id CLASS = -1;
constexpr Id END = -2;
constexpr Id INDEX = -3;
constexpr Id EXTENDS = -4;
constexpr Id ELIF = -5;
constexpr Id DEF = -6;
} // namespace TokenId
} // namespace Felidae
