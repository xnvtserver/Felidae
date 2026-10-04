// Stable grammar token IDs. Byte-level identifier tokens begin after this
// fixed range; do not renumber persisted/debug protocol identities casually.
#pragma once

#include <cstdint>

namespace Felidae {
inline constexpr int kFelidaeBuiltinTokenIds[] = {
    1, // IMPORT
    2, // NOT
    3, // AND
    4, // OR
    5, // THEN
    7, // IF
    8, // ELSE
    9, // RETURN
    10, // WHERE
    11, // EXTEND
    12, // LAMBDA
    13, // TRUE
    14, // FALSE
    15, // NIL
    16, // LPAREN
    17, // RPAREN
    18, // LBRACE
    19, // RBRACE
    20, // LBRACKET
    21, // RBRACKET
    22, // COMMA
    23, // COLON
    24, // DOT
    25, // PIPE
    26, // QUESTION
    27, // AT
    28, // ASSIGN
    29, // DOUBLE_COLON
    30, // ARROW
    31, // PLUS
    32, // MINUS
    33, // STAR
    34, // SLASH
    35, // PERCENT
    36, // EQUAL
    37, // NOT_EQUAL
    38, // LESS
    39, // LESS_EQUAL
    40, // GREATER
    41, // GREATER_EQUAL
    42, // QUOTE
    43, // BACKSLASH
    44, // COMMENT
    45, // SPACE
    46, // TAB
    47, // NEWLINE
    48, // CARRIAGE_RETURN
    49, // DIGIT_0
    50, // DIGIT_1
    51, // DIGIT_2
    52, // DIGIT_3
    53, // DIGIT_4
    54, // DIGIT_5
    55, // DIGIT_6
    56, // DIGIT_7
    57, // DIGIT_8
    58, // DIGIT_9
};

namespace TokenId {
using Id = std::int32_t;
constexpr Id UNKNOWN = 0;
constexpr Id IMPORT = 1;
constexpr Id NOT = 2;
constexpr Id AND = 3;
constexpr Id OR = 4;
constexpr Id THEN = 5;
// ID 6 is retired. It remains unused so stable grammar IDs are not renumbered.
constexpr Id RETIRED_6 = 6;
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
// Lexer-owned reserved words deliberately sit outside the byte-vocabulary IDs.
// They are syntax only and cannot collide with identifier bytes.
constexpr Id CLASS = -1;
constexpr Id END = -2;
constexpr Id INDEX = -3;
constexpr Id EXTENDS = -4;
constexpr Id ELIF = -5;
constexpr Id NEW = -6;
constexpr Id FOR = -7;
constexpr Id IN = -8;
constexpr Id WHILE = -9;
constexpr Id SWITCH = -10;
constexpr Id CASE = -11;
constexpr Id DEFAULT = -12;
constexpr Id BREAK = -13;
constexpr Id CONTINUE = -14;
constexpr Id DEF = -15;
constexpr Id THIS = -16;
constexpr Id SUPER = -17;
constexpr Id TRY = -18;
constexpr Id CATCH = -19;
constexpr Id VAR = -20;
constexpr Id ATOM_QUOTE = -21;
} // namespace TokenId
} // namespace Felidae
