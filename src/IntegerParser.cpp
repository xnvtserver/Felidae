#include "IntegerParser.h"

#include "IrCodeGenerator.h"
#include "MixfixStateModel.h"

#include "BuiltinRegistry.h"
#include "Operator.h"
#include "OperatorAnnotation.h"
#include "SentencePieceModel.h"
#include "form/SemanticOperation.h"

#include <sentencepiece.pb.h>
#include <sentencepiece_processor.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <unordered_map>

namespace Felidae {

namespace {
const SymbolId kMainSymbolId = symbolIdForName("main");

#ifndef NDEBUG
bool parserTraceEnabled() {
  static const bool enabled = [] {
    const auto *value = std::getenv("FELIDAE_TRACE");
    return value != nullptr && *value != '\0' && std::string_view(value) != "0";
  }();
  return enabled;
}
#endif

bool isFragmentableBuiltin(TokenId::Id id) {
  switch (id) {
  case TokenId::IMPORT:
  case TokenId::NOT:
  case TokenId::AND:
  case TokenId::OR:
  case TokenId::THEN:
  case TokenId::AS:
  case TokenId::IF:
  case TokenId::ELSE:
  case TokenId::RETURN:
  case TokenId::WHERE:
  case TokenId::EXTEND:
  case TokenId::LAMBDA:
  case TokenId::TRUE:
  case TokenId::FALSE:
  case TokenId::NIL:
    return true;
  default:
    return false;
  }
}

std::string pieceFragment(std::string piece) {
  // SentencePiece's whitespace marker has no lexical spelling once
  // IntegerParser has already skipped trivia. Byte-fallback pieces are
  // converted to their original byte so `re` + `turn` and byte fragments
  // participate in the same finite matching table.
  constexpr std::string_view marker = "\xE2\x96\x81";
  if (piece.starts_with(marker))
    piece.erase(0, marker.size());
  if (piece.size() == 6 && piece.starts_with("<0x") && piece[5] == '>') {
    const auto digit = [](char value) -> int {
      if (value >= '0' && value <= '9')
        return value - '0';
      if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
      return -1;
    };
    const int high = digit(piece[3]);
    const int low = digit(piece[4]);
    if (high >= 0 && low >= 0)
      return std::string(1, static_cast<char>((high << 4) | low));
  }
  return piece;
}

const std::vector<std::vector<int>> &builtinPieceSequences(TokenId::Id id) {
  static std::unordered_map<TokenId::Id, std::vector<std::vector<int>>> cache;
  if (const auto found = cache.find(id); found != cache.end())
    return found->second;
  auto &sequences = cache[id];
  const auto spelling = builtinTokenSpelling(id);
  if (!isFragmentableBuiltin(id) || spelling.empty())
    return sequences;
  const auto &model = felidaeSentencePieceModel();
  std::vector<std::pair<int, std::string>> pieces;
  pieces.reserve(static_cast<std::size_t>(model.GetPieceSize()));
  for (int pieceId = 0; pieceId < model.GetPieceSize(); ++pieceId) {
    auto fragment = pieceFragment(model.IdToPiece(pieceId));
    if (!fragment.empty() && fragment.size() <= spelling.size()) {
      pieces.emplace_back(pieceId, std::move(fragment));
    }
  }
  std::vector<int> current;
  const auto collect = [&](const auto &self, std::size_t offset) -> void {
    if (sequences.size() >= 64)
      return;
    if (offset == spelling.size()) {
      sequences.push_back(current);
      return;
    }
    for (const auto &[pieceId, fragment] : pieces) {
      if (!spelling.substr(offset).starts_with(fragment))
        continue;
      current.push_back(pieceId);
      self(self, offset + fragment.size());
      current.pop_back();
    }
  };
  collect(collect, 0);
  return sequences;
}

bool hasValueReturn(const std::vector<std::shared_ptr<Goal>> &goals) {
  for (const auto &goal : goals) {
    if (const auto returned = std::dynamic_pointer_cast<ReturnGoal>(goal)) {
      if (!returned->fields.empty())
        return true;
    }
  }
  return false;
}

bool isMethodStyleHead(const Call &head) {
  if (head.args.empty())
    return false;
  for (const auto &argument : head.args) {
    const auto type = std::dynamic_pointer_cast<VarExpr>(argument.value);
    if (argument.name.empty() || !type ||
        (type->languageTypeId == LanguageTypeId::Unknown &&
         !isFelidaeTypeAnnotationName(type->name))) {
      return false;
    }
  }
  return true;
}
} // namespace

IntegerParser::IntegerParser(const IntegerTokenList &input,
                             std::shared_ptr<OperatorRegistry> operators,
                             MixfixStateModel *mixfixModel)
    : input_(input), operators_(std::move(operators)),
      mixfixModel_(mixfixModel) {
  lineStarts_.push_back(0);
  for (std::size_t offset = 0; offset < input.source().size(); ++offset) {
    if (input.source()[offset] == '\r') {
      if (offset + 1 < input.source().size() &&
          input.source()[offset + 1] == '\n')
        ++offset;
      lineStarts_.push_back(offset + 1);
    } else if (input.source()[offset] == '\n') {
      lineStarts_.push_back(offset + 1);
    }
  }
  metrics_.sourceEncodeCount = input.encodeCount();
  metrics_.tokenCount = input.loadedSize();
#ifndef NDEBUG
  if (parserTraceEnabled()) {
    std::clog << "[felidae.parser] source_bytes=" << input.source().size()
              << " sentencepiece_tokens=" << metrics_.tokenCount
              << " encode_passes=" << metrics_.sourceEncodeCount << '\n';
  }
#endif
}

IntegerParser::RecursionScope::RecursionScope(IntegerParser &parser)
    : parser_(parser) {
  ++parser_.recursionDepth_;
  if (parser_.recursionDepth_ > kMaximumRecursionDepth) {
    --parser_.recursionDepth_;
    throw IntegerParserError("Maximum integer parser recursion depth exceeded");
  }
  parser_.metrics_.peakRecursionDepth =
      std::max(parser_.metrics_.peakRecursionDepth, parser_.recursionDepth_);
}

IntegerParser::RecursionScope::~RecursionScope() {
  if (parser_.recursionDepth_ != 0)
    --parser_.recursionDepth_;
}

void IntegerParser::step() {
  if (++metrics_.iterations > kMaximumIterations) {
    throw IntegerParserError("Integer parser iteration budget exceeded");
  }
}

void IntegerParser::alignPiece() {
  while (input_.has(piece_) && input_.entry(piece_).end <= byte_)
    ++piece_;
}

void IntegerParser::skipTrivia() {
  while (input_.has(piece_)) {
    step();
    const auto id = input_.entry(piece_).id;
    if (id == TokenId::SPACE || id == TokenId::TAB || id == TokenId::NEWLINE ||
        id == TokenId::CARRIAGE_RETURN) {
      byte_ = input_.entry(piece_++).end;
      continue;
    }
    if (id == TokenId::COMMENT) {
      byte_ = input_.entry(piece_++).end;
      while (input_.has(piece_) &&
             input_.entry(piece_).id != TokenId::NEWLINE &&
             input_.entry(piece_).id != TokenId::CARRIAGE_RETURN) {
        byte_ = input_.entry(piece_++).end;
      }
      continue;
    }
    break;
  }
  alignPiece();
}

std::size_t IntegerParser::builtinSequenceLength(TokenId::Id id) const {
  if (!input_.has(piece_))
    return 0;
  if (input_.entry(piece_).id == id) {
    // A word keyword ID immediately followed (no separator) by another
    // non-boundary piece is the leading fragment of one longer identifier
    // (`import` inside `imported`, `if` inside `ifRequired`, `true` inside
    // `trueValue`, ...), not a standalone reserved word here -- this mirrors
    // how isIdentifierBoundaryId already treats a keyword ID appearing mid
    // identifier, and how atNameRange() claims this same leading position.
    // at()/match() must refuse the match so callers such as the `if`-goal
    // and boolean-literal dispatch fall through to ordinary name parsing
    // instead of misreading a longer identifier as the reserved word.
    if (isKeywordWordTokenId(id) && input_.has(piece_ + 1) &&
        input_.entry(piece_ + 1).begin == input_.entry(piece_).end &&
        !isIdentifierBoundaryId(input_.entry(piece_ + 1).id)) {
      return 0;
    }
    return 1;
  }
  if (!isFragmentableBuiltin(id))
    return 0;
  for (const auto &sequence : builtinPieceSequences(id)) {
    if (sequence.size() < 2 || !input_.has(piece_ + sequence.size() - 1))
      continue;
    std::size_t cursor = byte_;
    bool matches = true;
    for (std::size_t index = 0; index < sequence.size(); ++index) {
      const auto &entry = input_.entry(piece_ + index);
      if (entry.id != sequence[index] || entry.begin > cursor ||
          entry.end <= cursor) {
        matches = false;
        break;
      }
      cursor = entry.end;
    }
    if (matches)
      return sequence.size();
  }
  return 0;
}

bool IntegerParser::at(TokenId::Id id) {
  skipTrivia();
  return builtinSequenceLength(id) != 0;
}

bool IntegerParser::match(TokenId::Id id) {
  if (!at(id))
    return false;
  const auto count = builtinSequenceLength(id);
  byte_ = input_.entry(piece_ + count - 1).end;
  piece_ += count;
  return true;
}

bool IntegerParser::atBlockEnd() {
  const auto savedByte = byte_;
  const auto savedPiece = piece_;
  const bool matched = matchBlockEnd();
  byte_ = savedByte;
  piece_ = savedPiece;
  return matched;
}

bool IntegerParser::matchBlockEnd() {
  const auto savedByte = byte_;
  const auto savedPiece = piece_;
  if (!atNameRange() || consumeNameRange() != "end") {
    byte_ = savedByte;
    piece_ = savedPiece;
    return false;
  }

  // `end` is contextual rather than a reserved tokenizer ID. It closes a
  // block only when it is the complete remaining word on its source line;
  // callable names and mixfix anchors containing `end` remain ordinary HIR.
  const bool lineBoundary = lineBreakBeforeNextSignificantPiece();
  const bool dotBoundary = !lineBoundary && at(TokenId::DOT);
  const bool inputBoundary = !lineBoundary && !dotBoundary && atEnd();
  if (!lineBoundary && !dotBoundary && !inputBoundary) {
    byte_ = savedByte;
    piece_ = savedPiece;
    return false;
  }
  if (dotBoundary)
    (void)match(TokenId::DOT);
  return true;
}

void IntegerParser::require(TokenId::Id id, const char *message) {
  if (!match(id)) {
    throw IntegerParserError(std::string(message) + " at source byte " +
                             std::to_string(byte_));
  }
}

bool IntegerParser::atEnd() {
  skipTrivia();
  const auto ended = !input_.has(piece_);
  metrics_.sourceEncodeCount = input_.encodeCount();
  metrics_.tokenCount = input_.loadedSize();
  return ended;
}

bool IntegerParser::atNameRange(bool allowLoneKeyword) {
  skipTrivia();
  if (!input_.has(piece_))
    return false;
  const auto id = input_.entry(piece_).id;
  // A qualified-name segment right after `.`/`:`/`::` (School.where,
  // Type.all, Type.select, ...) has no competing grammar interpretation: a
  // separator is never followed by the start of a new statement, so a
  // keyword spelling standing entirely alone there is unambiguously a name
  // segment, not the reserved word. Callers outside that continuation loop
  // never pass allowLoneKeyword, so `where`/`if`/... still open their normal
  // constructs everywhere else.
  if (allowLoneKeyword && isKeywordWordTokenId(id))
    return true;
  // A word keyword ID is atomic grammar vocabulary when it stands alone.
  // SentencePiece also emits that same ID as the leading piece of a longer
  // identifier -- `import` inside `imported`, `if` inside `ifRequired`,
  // `and` inside `andy`, `or` inside `orange`, `not` inside `notice`, and so
  // on for every word entry in kBuiltinTokens, not just `as`/`Assessment`.
  // Restricted to word keywords, never punctuation/digit builtin IDs: `.`
  // immediately followed by a digit (`190.0`) or a name (`csv.toFacts`) is
  // not an identifier fragment, and isIdentifierBoundaryId's own comment
  // already establishes that only keyword IDs get this treatment. A keyword
  // ID already stops being a boundary once an identifier range is under way
  // (see isIdentifierBoundaryId); the same contiguity test applies here so a
  // keyword ID at the very start of a name position gets the same treatment
  // instead of unconditionally reading as the reserved word.
  if (isKeywordWordTokenId(id) && input_.has(piece_ + 1) &&
      input_.entry(piece_ + 1).begin == input_.entry(piece_).end &&
      !isIdentifierBoundaryId(input_.entry(piece_ + 1).id)) {
    return true;
  }
  if (id == TokenId::AS) {
    // `foo(as: value)` uses `as` as a named-argument key: not merged with a
    // following identifier piece, but still a name position here, set apart
    // from the postfix `as` fact-designation keyword by the trailing colon.
    std::size_t following = piece_ + 1;
    while (input_.has(following) &&
           (input_.entry(following).id == TokenId::SPACE ||
            input_.entry(following).id == TokenId::TAB)) {
      ++following;
    }
    if (input_.has(following) && input_.entry(following).id == TokenId::COLON)
      return true;
  }
  return !isBuiltinTokenId(id) && id != TokenId::UNKNOWN;
}

bool IntegerParser::sourceContainsLineBreak(std::size_t begin,
                                            std::size_t end) const {
  return input_.source().find_first_of("\r\n", begin) < end;
}

bool IntegerParser::lineBreakBeforeNextSignificantPiece() const {
  bool inComment = false;
  for (std::size_t index = piece_; input_.has(index); ++index) {
    const auto id = input_.entry(index).id;
    if (id == TokenId::NEWLINE || id == TokenId::CARRIAGE_RETURN)
      return true;
    if (inComment)
      continue;
    if (id == TokenId::COMMENT) {
      inComment = true;
      continue;
    }
    if (id != TokenId::SPACE && id != TokenId::TAB)
      return false;
  }
  return false;
}

std::size_t IntegerParser::sourceLineIndent(std::size_t offset) const {
  const auto previous = offset == 0
                            ? std::string::npos
                            : input_.source().find_last_of("\r\n", offset - 1);
  const auto begin = previous == std::string::npos ? 0 : previous + 1;
  std::size_t indent = 0;
  for (auto index = begin; index < offset && index < input_.source().size();
       ++index) {
    if (input_.source()[index] == ' ')
      ++indent;
    else if (input_.source()[index] == '\t')
      indent += 4;
    else
      break;
  }
  return indent;
}

void IntegerParser::consumeStatementTerminator(std::size_t statementBegin) {
  if (match(TokenId::DOT) || atEnd())
    return;
  if (sourceContainsLineBreak(statementBegin, byte_))
    return;
  throw IntegerParserError(
      "Expected '.' or newline after statement at source byte " +
      std::to_string(byte_));
}

std::string IntegerParser::consumeNameRange(bool allowLoneKeyword) {
  skipTrivia();
  if (!atNameRange(allowLoneKeyword)) {
    const auto id =
        input_.has(piece_) ? input_.entry(piece_).id : TokenId::UNKNOWN;
    throw IntegerParserError(
        "Expected a SentencePiece name range at source byte " +
        std::to_string(byte_) + " (ID " + std::to_string(id) + ")");
  }
  const std::size_t begin = byte_;
  const std::size_t firstPiece = piece_;
  // A logical name is a contiguous run of non-grammar SentencePiece IDs.
  // SentencePiece may split one source name into many adjacent pieces.
  while (input_.has(piece_)) {
    const auto id = input_.entry(piece_).id;
    if (id == TokenId::UNKNOWN || isIdentifierBoundaryId(id))
      break;
    byte_ = input_.entry(piece_++).end;
  }
  if (byte_ == begin)
    throw IntegerParserError("Empty SentencePiece name range");
  auto spelling = input_.source().substr(begin, byte_ - begin);
  std::vector<std::uint32_t> pieces;
  pieces.reserve(piece_ - firstPiece);
  for (std::size_t index = firstPiece; index < piece_; ++index)
    pieces.push_back(static_cast<std::uint32_t>(input_.entry(index).id));
  (void)symbolIdForPieces(spelling, pieces);
  return spelling;
}

IntegerParser::StringLiteral IntegerParser::consumeString() {
  require(TokenId::QUOTE, "Expected a string literal");
  std::vector<int> ids;
  bool containsEscape = false;
  while (input_.has(piece_)) {
    const auto id = input_.entry(piece_).id;
    if (id == TokenId::QUOTE) {
      byte_ = input_.entry(piece_++).end;
      std::string value;
      const auto status = felidaeSentencePieceModel().Decode(ids, &value);
      if (!status.ok())
        throw IntegerParserError("Unable to decode string literal IDs");
      // SentencePiece faithfully decodes the lexical escape marker.  A
      // Felidae string value owns the escaped character instead, so do
      // this value-level normalization after decoding; grammar parsing
      // never falls back to scanning source characters.
      std::string unescaped;
      unescaped.reserve(value.size());
      for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '\\' || index + 1 == value.size()) {
          unescaped.push_back(value[index]);
          continue;
        }
        const char escaped = value[++index];
        switch (escaped) {
        case 'n':
          unescaped.push_back('\n');
          break;
        case 'r':
          unescaped.push_back('\r');
          break;
        case 't':
          unescaped.push_back('\t');
          break;
        case '\\':
          unescaped.push_back('\\');
          break;
        case '"':
          unescaped.push_back('"');
          break;
        default:
          unescaped.push_back('\\');
          unescaped.push_back(escaped);
          break;
        }
      }
      StringLiteral literal;
      literal.value = std::move(unescaped);
      literal.containsEscape = containsEscape;
      literal.sentencePieceIds.reserve(ids.size());
      for (const auto piece : ids) {
        if (piece < 0)
          throw IntegerParserError(
              "SentencePiece emitted an invalid string piece");
        literal.sentencePieceIds.push_back(static_cast<std::uint32_t>(piece));
      }
      return literal;
    }
    if (id == TokenId::BACKSLASH) {
      containsEscape = true;
      ids.push_back(id);
      byte_ = input_.entry(piece_++).end;
      if (!input_.has(piece_))
        throw IntegerParserError("Unterminated string escape");
    }
    ids.push_back(input_.entry(piece_).id);
    byte_ = input_.entry(piece_++).end;
  }
  throw IntegerParserError("Unterminated string literal");
}

double IntegerParser::consumeNumber() {
  skipTrivia();
  double value = 0.0;
  bool consumed = false;
  while (input_.has(piece_) && isDecimalDigitId(input_.entry(piece_).id)) {
    value = value * 10.0 +
            static_cast<double>(input_.entry(piece_).id - TokenId::DIGIT_0);
    byte_ = input_.entry(piece_++).end;
    consumed = true;
  }
  if (at(TokenId::DOT) && input_.has(piece_ + 1) &&
      isDecimalDigitId(input_.entry(piece_ + 1).id)) {
    match(TokenId::DOT);
    double scale = 0.1;
    while (input_.has(piece_) && isDecimalDigitId(input_.entry(piece_).id)) {
      value += static_cast<double>(input_.entry(piece_).id - TokenId::DIGIT_0) *
               scale;
      scale *= 0.1;
      byte_ = input_.entry(piece_++).end;
    }
  }
  if (!consumed)
    throw IntegerParserError("Expected a number literal");
  return value;
}

std::shared_ptr<Expr> IntegerParser::parseArray() {
  const std::size_t begin = byte_;
  require(TokenId::LBRACKET, "Expected '['");
  std::vector<std::shared_ptr<Expr>> items;
  if (!at(TokenId::RBRACKET)) {
    do {
      items.push_back(parseExpression());
    } while (match(TokenId::COMMA));
  }
  require(TokenId::RBRACKET, "Expected ']' after array");
  auto result = std::make_shared<ArrayExpr>(std::move(items));
  stamp(result, begin, byte_);
  return result;
}

std::vector<Arg> IntegerParser::parseArguments(bool allowAnnotationBindings) {
  require(TokenId::LPAREN, "Expected '('");
  std::vector<Arg> arguments;
  if (!at(TokenId::RPAREN)) {
    do {
      skipTrivia();
      const std::size_t before = byte_;
      QualifiedName name;
      bool named = false;
      if (atNameRange()) {
        const auto nameStart = byte_;
        const auto pieceStart = piece_;
        const auto candidate = consumeQualifiedName(false);
        if (match(TokenId::COLON)) {
          name = candidate;
          named = true;
        } else {
          byte_ = nameStart;
          piece_ = pieceStart;
        }
      }
      std::shared_ptr<Expr> value;
      if (allowAnnotationBindings && named && at(TokenId::LBRACKET)) {
        require(TokenId::LBRACKET, "Expected '[' for annotation bindings");
        std::vector<std::shared_ptr<Expr>> bindings;
        if (!at(TokenId::RBRACKET)) {
          do {
            const auto binding = consumeQualifiedName(false);
            require(TokenId::COLON,
                    "Expected ':' after annotation binding name");
            const auto type = consumeQualifiedName();
            auto typeExpr = std::make_shared<VarExpr>(
                type.spelling, type.nameId,
                languageTypeIdForName(type.spelling), type.isCapitalized);
            bindings.push_back(
                std::make_shared<MapExpr>(std::vector<MapEntry>{MapEntry{
                    binding.spelling, binding.nameId, std::move(typeExpr)}}));
          } while (match(TokenId::COMMA));
        }
        require(TokenId::RBRACKET, "Expected ']' after annotation bindings");
        value = std::make_shared<ArrayExpr>(std::move(bindings));
      } else if (allowAnnotationBindings && named && atNameRange()) {
        const auto valueByte = byte_;
        const auto valuePiece = piece_;
        const auto binding = consumeQualifiedName(false);
        if (match(TokenId::COLON)) {
          const auto type = consumeQualifiedName();
          auto typeExpr = std::make_shared<VarExpr>(
              type.spelling, type.nameId, languageTypeIdForName(type.spelling),
              type.isCapitalized);
          value = std::make_shared<MapExpr>(std::vector<MapEntry>{
              MapEntry{binding.spelling, binding.nameId, std::move(typeExpr)}});
        } else {
          byte_ = valueByte;
          piece_ = valuePiece;
        }
      }
      if (!value)
        value = parseExpression();
      if (byte_ == before)
        throw IntegerParserError(
            "Integer parser made no progress in argument list");
      arguments.emplace_back(named ? std::move(name.spelling) : std::string{},
                             named ? name.nameId : 0, std::move(value));
    } while (match(TokenId::COMMA));
  }
  require(TokenId::RPAREN, "Expected ')' after arguments");
  return arguments;
}

IntegerParser::QualifiedName
IntegerParser::consumeQualifiedName(bool allowNamespaceSeparators) {
  const auto firstPiece = piece_;
  const bool capitalized = input_.has(piece_) && isCapitalizedIdentifierStartId(
                                                     input_.entry(piece_).id);
  QualifiedName name{consumeNameRange(), 0, BuiltinId::Unknown, capitalized};
  while (at(TokenId::DOT) ||
         (allowNamespaceSeparators &&
          (at(TokenId::COLON) || at(TokenId::DOUBLE_COLON)))) {
    const auto beforeByte = byte_;
    const auto beforePiece = piece_;
    const auto separator = input_.entry(piece_).id;
    match(separator);
    const auto separatorEnd = byte_;
    // A separator is never followed by the start of a new statement, so a
    // keyword spelling standing entirely alone here (School.where,
    // Type.all, Type.select, ...) is unambiguously a name segment.
    if (!atNameRange(/*allowLoneKeyword=*/true) ||
        sourceContainsLineBreak(separatorEnd, byte_)) {
      byte_ = beforeByte;
      piece_ = beforePiece;
      break;
    }
    name.spelling += separator == TokenId::DOT     ? "."
                     : separator == TokenId::COLON ? ":"
                                                   : "::";
    name.spelling += consumeNameRange(/*allowLoneKeyword=*/true);
  }
  std::vector<TokenId::Id> ids;
  ids.reserve(piece_ - firstPiece);
  for (std::size_t index = firstPiece; index < piece_; ++index) {
    const auto id = input_.entry(index).id;
    if (id != TokenId::SPACE && id != TokenId::TAB && id != TokenId::NEWLINE &&
        id != TokenId::CARRIAGE_RETURN) {
      ids.push_back(id);
    }
  }
  std::vector<std::uint32_t> symbolPieces(ids.begin(), ids.end());
  name.nameId = symbolIdForPieces(name.spelling, symbolPieces);
  name.builtinId = builtinIdForPieceIds(ids);
  return name;
}

Call IntegerParser::parseCall() {
  const std::size_t begin = byte_;
  const auto name = consumeQualifiedName();
  if (!at(TokenId::LPAREN))
    throw IntegerParserError("Expected '(' after call name");
  Call result(name.spelling, name.nameId, parseArguments(), name.builtinId);
  result.sourceSpan = span(begin, byte_);
  return result;
}

Call IntegerParser::parseAnnotation() {
  require(TokenId::AT, "Expected '@'");
  const auto name = consumeQualifiedName();
  if (!at(TokenId::LPAREN)) {
    throw IntegerParserError("Annotation method '" + name.spelling +
                             "' requires an argument list");
  }
  return Call(name.spelling, name.nameId, parseArguments(true), name.builtinId);
}

const OperatorPatternDefinition &
IntegerParser::registerOperatorPattern(OperatorPatternDefinition pattern) {
  if (!operators_)
    throw IntegerParserError("Operator registry is unavailable");
  // Pattern structure is registered independently from source IDs. Literal
  // anchors use the absolute offsets in the line-wise SentencePiece stream;
  // do not encode each anchor again.
  OperatorRegistry::compilePattern(pattern);
  return operators_->registerPattern(std::move(pattern));
}

void IntegerParser::prepareOperatorAnnotation(const Call &annotation) {
  const bool overload = annotation.builtinId == BuiltinId::OverloadAnnotation ||
                        annotation.name == "overload";
  const bool mixfix = annotation.builtinId == BuiltinId::MixfixAnnotation ||
                      annotation.name == "mixfix";
  const bool matcherAnnotation =
      annotation.builtinId == BuiltinId::MatcherAnnotation ||
      annotation.name == "matcher";
  if (!operators_ || (!overload && !mixfix && !matcherAnnotation))
    return;
  // SentencePiece may fragment an annotation spelling differently while the
  // canonical decoded name remains exact. Normalize the parser-owned call
  // here rather than requiring one tokenization for language syntax.
  Call normalized = annotation;
  if (overload)
    normalized.builtinId = BuiltinId::OverloadAnnotation;
  else if (mixfix)
    normalized.builtinId = BuiltinId::MixfixAnnotation;
  else
    normalized.builtinId = BuiltinId::MatcherAnnotation;
  ParsedOperatorAnnotation parsed;
  try {
    parsed = decodeOperatorAnnotation(normalized);
  } catch (const std::runtime_error &error) {
    throw IntegerParserError(error.what());
  }
  const bool matcher = normalized.builtinId == BuiltinId::MatcherAnnotation;
  const OperatorPatternDefinition *pattern = nullptr;
  try {
    if (parsed.pattern.empty()) {
      pattern = operators_->findPatternByOperator(parsed.operatorName);
      if (!pattern)
        throw IntegerParserError(
            matcher
                ? "@matcher requires an operator pattern declared by @overload"
                : "Initial operator overload requires 'pattern'");
    } else {
      pattern = operators_->findPattern(parsed.operatorName, parsed.pattern);
      if (!pattern) {
        OperatorPatternDefinition definition;
        definition.operatorName = parsed.operatorName;
        definition.pattern = parsed.pattern;
        for (const auto &capture : parsed.captures)
          definition.captureTypeNames.push_back(capture.type);
        definition.precedence = parsed.precedence;
        definition.associativity = parsed.associativity;
        definition.fixity = parsed.fixity;
        definition.hasDeclaredFixity = parsed.hasFixity;
        definition.inferFixityFromPattern =
            parsed.kind == BuiltinId::MixfixAnnotation;
        definition.isMixfixDeclaration =
            parsed.kind == BuiltinId::MixfixAnnotation;
        definition.visibility = parsed.visibility;
        pattern = &registerOperatorPattern(std::move(definition));
      }
    }
    // The interpreter registers overload/matcher implementations only
    // after the annotated method clause is complete.  Parsing registers
    // syntax here so subsequent source can be assembled by integer IDs.
    (void)matcher;
  } catch (const std::runtime_error &error) {
    throw IntegerParserError(error.what());
  }
}

std::shared_ptr<Goal> IntegerParser::parseGoal() {
  skipTrivia();
  const std::size_t begin = byte_;
  if (match(TokenId::IF)) {
    auto conditionExpression = parseBinaryExpression(
        static_cast<int>(OperatorPrecedence::Control), TokenId::THEN);
    require(TokenId::THEN, "Expected 'then' after if condition");
    std::shared_ptr<Goal> condition;
    if (const auto comparison =
            std::dynamic_pointer_cast<OperatorExpression>(conditionExpression);
        comparison && comparison->captureCount() == 2 &&
        isComparisonOperator(comparison->coreOperator)) {
      condition = std::make_shared<BinaryGoal>(
          comparison->capture(0),
          coreOperatorDefinition(comparison->coreOperator).token,
          comparison->capture(1));
    } else {
      condition = std::make_shared<BinaryGoal>(
          std::move(conditionExpression), TokenId::EQUAL,
          std::make_shared<BoolExpr>(true));
    }
    auto thenBranch = parseGoalList(TokenId::DOT);
    std::vector<std::shared_ptr<Goal>> elseBranch;
    if (match(TokenId::ELSE))
      elseBranch = parseGoalList(TokenId::DOT);
    (void)matchBlockEnd();
    auto result = std::make_shared<IfGoal>(
        std::move(condition), std::move(thenBranch), std::move(elseBranch));
    stamp(result, begin, byte_);
    return result;
  }
  if (match(TokenId::WHERE)) {
    auto expression = parseExpression();
    // Mirrors the `if`-goal condition just above: a plain two-capture
    // comparison lowers straight to a BinaryGoal, and anything else --
    // `a and b`, `a or b`, a negation, a nested comparison chain -- lowers by
    // comparing the whole expression's truth value against `true`, the same
    // fallback `if` already relies on for compound conditions. `where` had
    // no such fallback and rejected every compound condition outright; nothing
    // else about guard-chain desugaring needed to change since it already
    // just consumes `where->condition` as an ordinary Goal.
    std::shared_ptr<Goal> condition;
    if (const auto comparison =
            std::dynamic_pointer_cast<OperatorExpression>(expression);
        comparison && comparison->captureCount() == 2 &&
        isComparisonOperator(comparison->coreOperator)) {
      condition = std::make_shared<BinaryGoal>(
          comparison->capture(0),
          coreOperatorDefinition(comparison->coreOperator).token,
          comparison->capture(1));
    } else {
      condition = std::make_shared<BinaryGoal>(
          std::move(expression), TokenId::EQUAL,
          std::make_shared<BoolExpr>(true));
    }
    auto result = std::make_shared<WhereGoal>(std::move(condition));
    stamp(result, begin, byte_);
    return result;
  }
  if (match(TokenId::LPAREN)) {
    auto grouped = parseGoalList(TokenId::RPAREN);
    require(TokenId::RPAREN, "Expected ')' after grouped goals");
    if (grouped.size() == 1)
      return grouped.front();
    auto result = std::make_shared<GroupGoal>(std::move(grouped));
    stamp(result, begin, byte_);
    return result;
  }
  if (match(TokenId::RETURN)) {
    std::vector<Arg> fields;
    // `match` intentionally skips trivia, therefore this boundary must
    // be observed before probing for the optional parenthesized form.
    const bool terminatedByLineBreak = lineBreakBeforeNextSignificantPiece();
    if (!terminatedByLineBreak && match(TokenId::LPAREN)) {
      if (!at(TokenId::RPAREN)) {
        do {
          skipTrivia();
          std::string name;
          const auto fieldByte = byte_;
          const auto fieldPiece = piece_;
          if (atNameRange()) {
            const auto candidate = consumeNameRange();
            if (match(TokenId::COLON))
              name = candidate;
            else {
              byte_ = fieldByte;
              piece_ = fieldPiece;
            }
          }
          fields.emplace_back(std::move(name), parseExpression());
        } while (match(TokenId::COMMA));
      }
      require(TokenId::RPAREN, "Expected ')' after return fields");
    } else {
      // `return value` is the established method form.  The source is
      // already one SentencePiece stream; this merely assembles the
      // following integer range as an expression rather than leaving it
      // to be misread as the next top-level declaration.
      skipTrivia();
      if (!terminatedByLineBreak && !atEnd() && !at(TokenId::ELSE) &&
          !at(TokenId::DOT)) {
        fields.emplace_back("", parseExpression());
      }
    }
    auto result = std::make_shared<ReturnGoal>(std::move(fields));
    stamp(result, begin, byte_);
    return result;
  }
  if (match(TokenId::NOT)) {
    auto result = std::make_shared<NotGoal>(parseCall());
    stamp(result, begin, byte_);
    return result;
  }
  const auto start = byte_;
  const auto startPiece = piece_;
  if (atNameRange()) {
    const auto name = consumeNameRange();
    if (match(TokenId::ASSIGN)) {
      auto result = std::make_shared<AssignGoal>(name, parseExpression());
      stamp(result, begin, byte_);
      return result;
    }
  }
  byte_ = start;
  piece_ = startPiece;
  auto left = parseExpression();
  if (match(TokenId::AS)) {
    throw IntegerParserError(
        "postfix 'as' fact designations are deprecated; use concrete fact "
        "type where/select operations");
  }
  if (const auto comparison =
          std::dynamic_pointer_cast<OperatorExpression>(left);
      comparison && comparison->captureCount() == 2 &&
      isComparisonOperator(comparison->coreOperator)) {
    const auto definition = coreOperatorDefinition(comparison->coreOperator);
    auto result = std::make_shared<BinaryGoal>(
        comparison->capture(0), definition.token, comparison->capture(1));
    stamp(result, begin, byte_);
    return result;
  }
  skipTrivia();
  if (input_.has(piece_) && input_.entry(piece_).begin == byte_) {
    const auto definition = infixOperatorForId(input_.entry(piece_).id);
    if (definition && isComparisonOperator(definition->id)) {
      match(input_.entry(piece_).id);
      auto result = std::make_shared<BinaryGoal>(
          std::move(left), definition->token, parseExpression());
      stamp(result, begin, byte_);
      return result;
    }
  }
  const auto term = std::dynamic_pointer_cast<TermExpr>(left);
  if (!term)
    throw IntegerParserError("Expected a predicate call or comparison goal");
  Call call(term->name, term->nameId, term->args, term->builtinId);
  auto result = std::make_shared<CallGoal>(std::move(call));
  stamp(result, begin, byte_);
  return result;
}

std::vector<std::shared_ptr<Goal>>
IntegerParser::parseGoalList(TokenId::Id terminator) {
  std::vector<std::shared_ptr<Goal>> goals;
  if (at(terminator))
    return goals;
  std::size_t bodyIndent = 0;
  bool hasBodyIndent = false;
  do {
    // Measure indentation at the first significant ID, not at the
    // preceding arrow/terminator.  This keeps a bare return from pulling
    // the next top-level declaration into its method body.
    skipTrivia();
    if (atBlockEnd())
      break;
    const auto before = byte_;
    if (!hasBodyIndent) {
      bodyIndent = sourceLineIndent(before);
      hasBodyIndent = true;
    }
    goals.push_back(parseGoal());
    if (byte_ == before)
      throw IntegerParserError("Integer parser made no progress in goal list");
    if (const auto returned =
            std::dynamic_pointer_cast<ReturnGoal>(goals.back());
        returned && returned->fields.empty() &&
        sourceContainsLineBreak(before, byte_)) {
      break;
    }
    if (match(TokenId::COMMA))
      continue;
    if (atEnd() || at(terminator) || at(TokenId::ELSE) || atBlockEnd())
      break;
    if (!sourceContainsLineBreak(before, byte_) ||
        sourceLineIndent(byte_) < bodyIndent)
      break;
    if (bodyIndent == 0) {
      // Zero-indented multi-line method bodies are supported for
      // compatibility.  They are ambiguous with the next top-level
      // declaration, so detect only unambiguous statement starters
      // without re-tokenizing source: annotations/imports, a callable
      // clause head. An assignment remains part of this body: without
      // indentation, a local assignment and a following global binding
      // are otherwise indistinguishable, and method-local semantics
      // take precedence until an explicit declaration boundary.
      if (at(TokenId::AT) || at(TokenId::IMPORT))
        break;
      const auto statementByte = byte_;
      const auto statementPiece = piece_;
      bool topLevelStatement = false;
      if (atNameRange()) {
        (void)consumeQualifiedName();
        topLevelStatement = at(TokenId::LPAREN);
      }
      byte_ = statementByte;
      piece_ = statementPiece;
      if (topLevelStatement)
        break;
    }
  } while (true);
  return goals;
}

std::shared_ptr<Statement> IntegerParser::parseStatement() {
  skipTrivia();
  const std::size_t begin = byte_;
  std::vector<Call> annotations;
  while (at(TokenId::AT)) {
    annotations.push_back(parseAnnotation());
    skipTrivia();
  }
  for (const auto &annotation : annotations)
    prepareOperatorAnnotation(annotation);
  if (match(TokenId::IMPORT)) {
    if (!annotations.empty())
      throw IntegerParserError(
          "Annotations can only be applied to method declarations");
    std::vector<std::string> paths;
    if (match(TokenId::LPAREN)) {
      if (!at(TokenId::RPAREN)) {
        do {
          paths.push_back(consumeString().value);
        } while (match(TokenId::COMMA));
      }
      require(TokenId::RPAREN, "Expected ')' after import paths");
    } else {
      paths.push_back(consumeString().value);
    }
    consumeStatementTerminator(begin);
    auto result = std::make_shared<ImportStmt>(std::move(paths));
    stamp(result, begin, byte_);
    return result;
  }
  const auto checkpoint = byte_;
  const auto checkpointPiece = piece_;
  if (atNameRange()) {
    const auto name = consumeNameRange();
    if (match(TokenId::ASSIGN)) {
      if (!annotations.empty())
        throw IntegerParserError(
            "Annotations can only be applied to method declarations");
      auto result =
          std::make_shared<GlobalBindingStmt>(name, parseExpression());
      consumeStatementTerminator(begin);
      stamp(result, begin, byte_);
      return result;
    }
  }
  byte_ = checkpoint;
  piece_ = checkpointPiece;
  // Native and user clauses may use qualified heads such as `math.sin`.
  // The separators are already atomic grammar IDs, so assemble the entire
  // head before requiring its argument list.
  const auto clauseName = consumeQualifiedName();
  std::vector<std::string> parentNames;
  if (match(TokenId::EXTEND)) {
    do {
      parentNames.push_back(consumeQualifiedName().spelling);
    } while (match(TokenId::COMMA));
  }
  if (!at(TokenId::LPAREN)) {
    throw IntegerParserError("Expected '(' after clause name '" +
                             clauseName.spelling + "' at source byte " +
                             std::to_string(byte_));
  }
  Call head(clauseName.spelling, clauseName.nameId, parseArguments(),
            clauseName.builtinId);
  if (match(TokenId::AS)) {
    throw IntegerParserError(
        "postfix 'as' fact designations are deprecated; use concrete fact "
        "type where/select operations");
  }
  std::vector<std::shared_ptr<Goal>> body;
  std::vector<std::vector<std::shared_ptr<Goal>>> fallbackBranches;
  bool emptyDeclaration = false;
  if (match(TokenId::ARROW)) {
    if (match(TokenId::LPAREN)) {
      require(TokenId::RPAREN, "Expected ')' after empty declaration");
      emptyDeclaration = true;
    } else if (match(TokenId::LBRACE)) {
      require(TokenId::RBRACE, "Expected '}' after empty declaration");
      emptyDeclaration = true;
    } else {
      body = parseGoalList(TokenId::DOT);
      while (match(TokenId::ELSE)) {
        const auto beforeBranch = byte_;
        auto branch = parseGoalList(TokenId::DOT);
        if (branch.empty() || byte_ == beforeBranch) {
          throw IntegerParserError("Expected fallback branch after 'else'");
        }
        fallbackBranches.push_back(std::move(branch));
      }
      (void)matchBlockEnd();
    }
  }
  consumeStatementTerminator(begin);
  // Annotations describe callable operator implementations.  They are
  // methods even when their body has a bare `return` (or no value return),
  // so classification must not depend solely on ReturnGoal fields.
  const ClauseKind kind =
      emptyDeclaration
          ? ClauseKind::NativeDeclaration
          : (head.nameId == kMainSymbolId || !annotations.empty() ||
                     isMethodStyleHead(head) || hasValueReturn(body) ||
                     !fallbackBranches.empty()
                 ? ClauseKind::Method
             : body.empty() ? ClauseKind::Fact
                            : ClauseKind::Rule);
  auto result = std::make_shared<ClauseStmt>(
      std::move(head), std::move(parentNames), std::move(body),
      std::move(fallbackBranches), emptyDeclaration, kind);
  if (!annotations.empty() && result->clauseKind != ClauseKind::Method) {
    throw IntegerParserError(
        "Annotations can only be applied to complete method declarations");
  }
  result->annotations = std::move(annotations);
  for (const auto &annotation : result->annotations)
    registerOperatorImplementation(annotation, *result);
  stamp(result, begin, byte_);
  return result;
}

Program IntegerParser::parseProgram() {
#ifndef NDEBUG
  if (parserTraceEnabled())
    std::clog << "[felidae.parser] action=parse_program begin\n";
#endif
  Program program;
  while (!atEnd()) {
    const auto before = byte_;
    program.addStatement(parseStatement());
    ++metrics_.statementCount;
    if (byte_ == before)
      throw IntegerParserError("Integer parser made no progress in program");
  }
#ifndef NDEBUG
  if (parserTraceEnabled()) {
    std::clog << "[felidae.parser] action=parse_program complete statements="
              << metrics_.statementCount << " final_byte=" << byte_ << '\n';
  }
#endif
  return program;
}

std::vector<std::shared_ptr<Goal>> IntegerParser::parseQuery() {
  match(TokenId::QUESTION);
  auto goals = parseGoalList(TokenId::DOT);
  if (match(TokenId::DOT) && !atEnd()) {
    throw IntegerParserError("Unexpected source after query terminator");
  }
  if (!atEnd())
    throw IntegerParserError("Expected end of query");
  return goals;
}

bool IntegerParser::startsQuery() { return at(TokenId::QUESTION); }

std::shared_ptr<Expr> IntegerParser::parseMap() {
  const std::size_t begin = byte_;
  require(TokenId::LBRACE, "Expected '{'");
  std::vector<MapEntry> entries;
  if (!at(TokenId::RBRACE)) {
    do {
      const auto key = consumeNameRange();
      require(TokenId::COLON, "Expected ':' after map key");
      entries.emplace_back(key, parseExpression());
    } while (match(TokenId::COMMA));
  }
  require(TokenId::RBRACE, "Expected '}' after map");
  auto result = std::make_shared<MapExpr>(std::move(entries));
  stamp(result, begin, byte_);
  return result;
}

std::shared_ptr<Expr> IntegerParser::parsePrimary() {
  RecursionScope recursion(*this);
  skipTrivia();
  const std::size_t begin = byte_;
  if (at(TokenId::QUOTE)) {
    auto literal = consumeString();
    auto result = std::make_shared<StringExpr>(
        std::move(literal.value), std::move(literal.sentencePieceIds),
        literal.containsEscape);
    stamp(result, begin, byte_);
    return result;
  }
  if (input_.has(piece_) && isDecimalDigitId(input_.entry(piece_).id)) {
    auto number = consumeNumber();
    // A postfix percent is a numeric literal, not modulo: `75%` is the
    // threshold 0.75. `%` remains modulo whenever a right operand follows.
    const auto beforePercentByte = byte_;
    const auto beforePercentPiece = piece_;
    // Percent is postfix only when immediately attached to its number.
    // Whitespace-delimited `%` remains the binary modulo operator.
    if (byte_ > 0 && byte_ < input_.source().size() &&
        input_.source()[byte_ - 1] != ' ' &&
        input_.source()[byte_ - 1] != '\t' && input_.source()[byte_] == '%' &&
        input_.has(piece_) && input_.entry(piece_).id == TokenId::PERCENT) {
      byte_ = input_.entry(piece_++).end;
      const bool hasRightOperand =
          (input_.has(piece_) && isDecimalDigitId(input_.entry(piece_).id)) ||
          atNameRange() || at(TokenId::LPAREN) || at(TokenId::LBRACKET) ||
          at(TokenId::LBRACE);
      if (hasRightOperand) {
        byte_ = beforePercentByte;
        piece_ = beforePercentPiece;
      } else
        number /= 100.0;
    }
    auto result = std::make_shared<NumberExpr>(number);
    stamp(result, begin, byte_);
    return result;
  }
  if (match(TokenId::TRUE)) {
    auto result = std::make_shared<BoolExpr>(true);
    stamp(result, begin, byte_);
    return result;
  }
  if (match(TokenId::FALSE)) {
    auto result = std::make_shared<BoolExpr>(false);
    stamp(result, begin, byte_);
    return result;
  }
  if (match(TokenId::NIL)) {
    auto result = std::make_shared<NilExpr>();
    stamp(result, begin, byte_);
    return result;
  }
  if (match(TokenId::LAMBDA)) {
    require(TokenId::LPAREN, "Expected '(' after lambda");
    auto source = parseExpression();
    require(TokenId::COMMA, "Expected ',' after lambda source");
    const auto variable = consumeNameRange();
    require(TokenId::ARROW, "Expected '=>' after lambda variable");
    auto body = parseExpression();
    require(TokenId::RPAREN, "Expected ')' after lambda");
    auto result = std::make_shared<LambdaExpr>(std::move(source), variable,
                                               std::move(body));
    stamp(result, begin, byte_);
    return result;
  }
  if (at(TokenId::LBRACKET))
    return parseArray();
  if (at(TokenId::LBRACE))
    return parseMap();
  if (match(TokenId::LPAREN)) {
    auto result = parseExpression();
    require(TokenId::RPAREN, "Expected ')' after grouped expression");
    stamp(result, begin, byte_);
    return result;
  }
  if (atNameRange()) {
    const auto name = consumeQualifiedName();
    if (at(TokenId::LPAREN)) {
      auto result = std::make_shared<TermExpr>(name.spelling, name.nameId,
                                               parseArguments(), name.builtinId,
                                               name.isCapitalized);
      stamp(result, begin, byte_);
      return result;
    }
    auto result = std::make_shared<VarExpr>(
        name.spelling, name.nameId, languageTypeIdForName(name.spelling),
        name.isCapitalized);
    stamp(result, begin, byte_);
    return result;
  }
  throw IntegerParserError("Expected an expression at source byte " +
                           std::to_string(byte_));
}

std::shared_ptr<Expr> IntegerParser::parseExpression() {
  return parseBinaryExpression(static_cast<int>(OperatorPrecedence::Control));
}

bool IntegerParser::atPatternLexeme(const PatternLexeme &lexeme) {
  const auto savedByte = byte_;
  const auto savedPiece = piece_;
  const bool matched = matchPatternLexeme(lexeme);
  byte_ = savedByte;
  piece_ = savedPiece;
  return matched;
}

bool IntegerParser::matchPatternLexeme(const PatternLexeme &lexeme) {
  const auto savedByte = byte_;
  const auto savedPiece = piece_;
  skipTrivia();
  std::size_t wanted = 0;
  while (!lexeme.pieceIds.empty() && wanted < lexeme.pieceIds.size()) {
    if (!input_.has(piece_) || input_.entry(piece_).begin > byte_ ||
        input_.entry(piece_).end <= byte_) {
      break;
    }
    if (input_.entry(piece_).id != lexeme.pieceIds[wanted]) {
      break;
    }
    ++wanted;
    byte_ = input_.entry(piece_++).end;
  }
  if (!lexeme.pieceIds.empty() && wanted == lexeme.pieceIds.size())
    return true;

  // Anchors are registered once, but SentencePiece IDs are emitted from the
  // current source line. The same spelling may therefore be one piece,
  // several pieces, or carry a neighbouring whitespace piece.  Use the
  // original encode offsets to recognize the registered literal spelling;
  // this is not a second tokenizer or a string grammar.
  byte_ = savedByte;
  piece_ = savedPiece;
  skipTrivia();
  const auto start = byte_;
  const auto end = start + lexeme.spelling.size();
  const auto &source = input_.source();
  if (end > source.size() ||
      source.compare(start, lexeme.spelling.size(), lexeme.spelling) != 0) {
    byte_ = savedByte;
    piece_ = savedPiece;
    return false;
  }
  // A word-shaped anchor must end at a source identifier boundary. Without
  // this, `plan` could incorrectly assemble inside `planar` when the model
  // fragments that identifier differently from a declaration anchor.
  const auto isIdentifierByte = [](unsigned char value) {
    return std::isalnum(value) != 0 || value == '_';
  };
  if (!lexeme.spelling.empty() &&
      isIdentifierByte(static_cast<unsigned char>(lexeme.spelling.back())) &&
      end < source.size() &&
      isIdentifierByte(static_cast<unsigned char>(source[end]))) {
    byte_ = savedByte;
    piece_ = savedPiece;
    return false;
  }
  while (input_.has(piece_) && input_.entry(piece_).end <= end)
    ++piece_;
  if (input_.has(piece_) && input_.entry(piece_).begin < end) {
    byte_ = savedByte;
    piece_ = savedPiece;
    return false;
  }
  byte_ = end;
  return true;
}

bool IntegerParser::atPatternAnchor(const std::vector<PatternLexeme> &anchor) {
  const auto savedByte = byte_;
  const auto savedPiece = piece_;
  const bool matched = matchPatternAnchor(anchor);
  byte_ = savedByte;
  piece_ = savedPiece;
  return matched;
}

bool IntegerParser::matchPatternAnchor(
    const std::vector<PatternLexeme> &anchor) {
  const auto savedByte = byte_;
  const auto savedPiece = piece_;
  for (const auto &lexeme : anchor) {
    if (matchPatternLexeme(lexeme))
      continue;
    byte_ = savedByte;
    piece_ = savedPiece;
    return false;
  }
  return true;
}

std::shared_ptr<Expr> IntegerParser::tryParseLeadingPattern() {
  if (!operators_)
    return {};
  const auto startByte = byte_;
  const auto startPiece = piece_;
  std::shared_ptr<OperatorExpression> selected;
  std::size_t selectedByte = startByte;
  std::size_t selectedPiece = startPiece;
  for (const auto &pattern : operators_->patterns()) {
    if (pattern.startsWithCapture || pattern.anchorLexemes.empty() ||
        pattern.anchorLexemes.front().empty())
      continue;
    ++metrics_.backtrackingAttempts;
    if (!atPatternLexeme(pattern.anchorLexemes.front().front()))
      continue;
    byte_ = startByte;
    piece_ = startPiece;
    bool candidateParsed = false;
    try {
      if (!matchPatternAnchor(pattern.anchorLexemes.front()))
        continue;
      std::vector<OperatorCapture> captures;
      captures.reserve(pattern.captureNames.size());
      for (std::size_t index = 0; index < pattern.captureNames.size();
           ++index) {
        const bool adjacent =
            index + 1 < pattern.captureNames.size() &&
            (index >= pattern.followingAnchorIndices.size() ||
             !pattern.followingAnchorIndices[index].has_value());
        const auto following = index < pattern.followingAnchorIndices.size()
                                   ? pattern.followingAnchorIndices[index]
                                   : std::optional<std::size_t>{};
        const auto *stopAnchor =
            following && *following < pattern.anchorLexemes.size()
                ? &pattern.anchorLexemes[*following]
                : nullptr;
        auto captured =
            adjacent
                ? parseUnary()
                : parseBinaryExpression(static_cast<int>(pattern.precedence),
                                        TokenId::UNKNOWN, stopAnchor);
        captures.emplace_back(pattern.captureNames[index], std::move(captured));
        if (following &&
            (*following >= pattern.anchorLexemes.size() ||
             !matchPatternAnchor(pattern.anchorLexemes[*following]))) {
          throw IntegerParserError(
              "mixfix candidate did not consume its next anchor");
        }
      }
      auto candidate = std::make_shared<OperatorExpression>(
          pattern.operatorId, pattern.patternId, std::move(captures));
      candidateParsed = true;
      stamp(candidate, startByte, byte_);
      candidate->resolvedMethodId = resolveMixfixMethod(*candidate);
      if (candidate->resolvedMethodId == 0 && mixfixModel_) {
        candidate->resolvedMethodId = resolveModelMixfixMethod(candidate);
      }
      if (!selected || byte_ > selectedByte ||
          (byte_ == selectedByte && candidate->resolvedMethodId != 0 &&
           selected->resolvedMethodId == 0)) {
        selected = std::move(candidate);
        selectedByte = byte_;
        selectedPiece = piece_;
      }
    } catch (const IntegerParserError &) {
      // Once the full syntax shape has parsed, an SSM/IR verification
      // error is authoritative. Do not disguise it as an unrelated
      // leftover-token parse failure by backtracking to a plain name.
      if (candidateParsed)
        throw;
      // Another candidate sharing this integer anchor may still match.
    }
    byte_ = startByte;
    piece_ = startPiece;
  }
  if (!selected)
    return {};
  byte_ = selectedByte;
  piece_ = selectedPiece;
  return selected;
}

std::shared_ptr<Expr>
IntegerParser::tryParseTrailingPattern(std::shared_ptr<Expr> left,
                                       int minimumPrecedence) {
  if (!operators_)
    return {};
  const auto leftSpan = left->sourceSpan;
  const auto startByte = byte_;
  const auto startPiece = piece_;
  const auto stampTrailing =
      [&](const std::shared_ptr<OperatorExpression> &expression,
          std::size_t endByte) {
        expression->sourceSpan = leftSpan;
        const auto ending = span(startByte, endByte);
        expression->sourceSpan.endLine = ending.endLine;
        expression->sourceSpan.endColumn = ending.endColumn;
      };
  std::shared_ptr<OperatorExpression> immediate;
  std::size_t immediateByte = startByte;
  std::size_t immediatePiece = startPiece;
  for (const auto &pattern : operators_->patterns()) {
    if (!pattern.startsWithCapture || pattern.captureNames.empty() ||
        pattern.anchorLexemes.empty() ||
        pattern.anchorLexemes.front().empty() ||
        static_cast<int>(pattern.precedence) < minimumPrecedence)
      continue;
    ++metrics_.backtrackingAttempts;
    if (!atPatternLexeme(pattern.anchorLexemes.front().front()))
      continue;
    byte_ = startByte;
    piece_ = startPiece;
    bool candidateParsed = false;
    try {
      if (!matchPatternAnchor(pattern.anchorLexemes.front()))
        continue;
      std::vector<OperatorCapture> captures;
      captures.reserve(pattern.captureNames.size());
      captures.emplace_back(pattern.captureNames.front(), left->clone());
      for (std::size_t index = 1; index < pattern.captureNames.size();
           ++index) {
        const bool adjacent =
            index + 1 < pattern.captureNames.size() &&
            (index >= pattern.followingAnchorIndices.size() ||
             !pattern.followingAnchorIndices[index].has_value());
        const auto following = index < pattern.followingAnchorIndices.size()
                                   ? pattern.followingAnchorIndices[index]
                                   : std::optional<std::size_t>{};
        const auto *stopAnchor =
            following && *following < pattern.anchorLexemes.size()
                ? &pattern.anchorLexemes[*following]
                : nullptr;
        auto captured =
            adjacent
                ? parseUnary()
                : parseBinaryExpression(static_cast<int>(pattern.precedence),
                                        TokenId::UNKNOWN, stopAnchor);
        captures.emplace_back(pattern.captureNames[index], std::move(captured));
        if (following &&
            (*following >= pattern.anchorLexemes.size() ||
             !matchPatternAnchor(pattern.anchorLexemes[*following]))) {
          throw IntegerParserError(
              "trailing mixfix candidate did not consume its next anchor");
        }
      }
      if (!immediate || byte_ > immediateByte) {
        immediate = std::make_shared<OperatorExpression>(
            pattern.operatorId, pattern.patternId, std::move(captures));
        candidateParsed = true;
        stampTrailing(immediate, byte_);
        immediate->resolvedMethodId = resolveMixfixMethod(*immediate);
        if (immediate->resolvedMethodId == 0 && mixfixModel_) {
          immediate->resolvedMethodId = resolveModelMixfixMethod(immediate);
        }
        immediateByte = byte_;
        immediatePiece = piece_;
      }
    } catch (const IntegerParserError &) {
      if (candidateParsed)
        throw;
      // Another pattern sharing this anchor may still match.
    }
    byte_ = startByte;
    piece_ = startPiece;
  }
  if (immediate) {
    byte_ = immediateByte;
    piece_ = immediatePiece;
    return immediate;
  }
  const OperatorPatternDefinition *selected = nullptr;
  std::size_t selectedLength = 0;
  for (const auto &pattern : operators_->patterns()) {
    if (!pattern.startsWithCapture || pattern.captureNames.empty() ||
        pattern.anchorLexemes.empty() ||
        pattern.anchorLexemes.front().empty() ||
        static_cast<int>(pattern.precedence) < minimumPrecedence)
      continue;
    ++metrics_.backtrackingAttempts;
    if (!atPatternLexeme(pattern.anchorLexemes.front().front()))
      continue;
    const auto length = pattern.anchorLexemes.front().size();
    if (selected && length == selectedLength) {
      throw IntegerParserError("Ambiguous integer mixfix anchor sequence");
    }
    if (!selected || length > selectedLength) {
      selected = &pattern;
      selectedLength = length;
    }
  }
  if (!selected) {
    const auto deferredStartByte = byte_;
    const auto deferredStartPiece = piece_;
    std::shared_ptr<OperatorExpression> deferred;
    std::size_t deferredByte = deferredStartByte;
    std::size_t deferredPiece = deferredStartPiece;
    for (const auto *pattern : operators_->deferredTrailingCapturePatterns()) {
      if (static_cast<int>(pattern->precedence) < minimumPrecedence)
        continue;
      ++metrics_.backtrackingAttempts;
      byte_ = deferredStartByte;
      piece_ = deferredStartPiece;
      bool candidateParsed = false;
      try {
        std::vector<OperatorCapture> captures;
        captures.reserve(pattern->captureNames.size());
        captures.emplace_back(pattern->captureNames.front(), left->clone());
        for (std::size_t index = 1; index < pattern->captureNames.size();
             ++index) {
          const bool adjacent =
              index + 1 < pattern->captureNames.size() &&
              (index >= pattern->followingAnchorIndices.size() ||
               !pattern->followingAnchorIndices[index].has_value());
          const auto following = index < pattern->followingAnchorIndices.size()
                                     ? pattern->followingAnchorIndices[index]
                                     : std::optional<std::size_t>{};
          const auto *stopAnchor =
              following && *following < pattern->anchorLexemes.size()
                  ? &pattern->anchorLexemes[*following]
                  : nullptr;
          auto captured =
              adjacent
                  ? parseUnary()
                  : parseBinaryExpression(static_cast<int>(pattern->precedence),
                                          TokenId::UNKNOWN, stopAnchor);
          captures.emplace_back(pattern->captureNames[index],
                                std::move(captured));
          if (following &&
              (*following >= pattern->anchorLexemes.size() ||
               !matchPatternAnchor(pattern->anchorLexemes[*following]))) {
            throw IntegerParserError(
                "deferred mixfix candidate did not consume its next anchor");
          }
        }
        if (!deferred || byte_ > deferredByte) {
          deferred = std::make_shared<OperatorExpression>(
              pattern->operatorId, pattern->patternId, std::move(captures));
          candidateParsed = true;
          stampTrailing(deferred, byte_);
          deferred->resolvedMethodId = resolveMixfixMethod(*deferred);
          if (deferred->resolvedMethodId == 0 && mixfixModel_) {
            deferred->resolvedMethodId = resolveModelMixfixMethod(deferred);
          }
          deferredByte = byte_;
          deferredPiece = piece_;
        }
      } catch (const IntegerParserError &) {
        if (candidateParsed)
          throw;
        // A different deferred shape may consume this same ID range.
      }
    }
    if (!deferred) {
      byte_ = deferredStartByte;
      piece_ = deferredStartPiece;
      return {};
    }
    byte_ = deferredByte;
    piece_ = deferredPiece;
    return deferred;
  }
  if (!matchPatternAnchor(selected->anchorLexemes.front())) {
    throw IntegerParserError(
        "Integer mixfix anchor disappeared during assembly");
  }
  std::vector<OperatorCapture> captures;
  captures.reserve(selected->captureNames.size());
  captures.emplace_back(selected->captureNames.front(), std::move(left));
  for (std::size_t index = 1; index < selected->captureNames.size(); ++index) {
    const bool adjacent =
        index + 1 < selected->captureNames.size() &&
        (index >= selected->followingAnchorIndices.size() ||
         !selected->followingAnchorIndices[index].has_value());
    const auto following = index < selected->followingAnchorIndices.size()
                               ? selected->followingAnchorIndices[index]
                               : std::optional<std::size_t>{};
    const auto *stopAnchor =
        following && *following < selected->anchorLexemes.size()
            ? &selected->anchorLexemes[*following]
            : nullptr;
    auto captured =
        adjacent ? parseUnary()
                 : parseBinaryExpression(static_cast<int>(selected->precedence),
                                         TokenId::UNKNOWN, stopAnchor);
    captures.emplace_back(selected->captureNames[index], std::move(captured));
    if (index < selected->followingAnchorIndices.size() &&
        selected->followingAnchorIndices[index]) {
      const auto anchorIndex = *selected->followingAnchorIndices[index];
      if (anchorIndex >= selected->anchorLexemes.size() ||
          !matchPatternAnchor(selected->anchorLexemes[anchorIndex])) {
        throw IntegerParserError(
            "Expected integer mixfix literal anchor for '" +
            selected->operatorName + "' at source byte " +
            std::to_string(byte_));
      }
    }
  }
  auto expression = std::make_shared<OperatorExpression>(
      selected->operatorId, selected->patternId, std::move(captures));
  stampTrailing(expression, byte_);
  expression->resolvedMethodId = resolveMixfixMethod(*expression);
  if (expression->resolvedMethodId == 0 && mixfixModel_) {
    expression->resolvedMethodId = resolveModelMixfixMethod(expression);
  }
  return expression;
}

std::shared_ptr<Expr> IntegerParser::parseUnary() {
  if (auto pattern = tryParseLeadingPattern())
    return pattern;
  if (match(TokenId::NOT))
    return std::make_shared<OperatorExpression>(CoreOperator::LogicalNot,
                                                parseUnary());
  if (match(TokenId::MINUS)) {
    auto operand = parseUnary();
    if (const auto number = std::dynamic_pointer_cast<NumberExpr>(operand)) {
      return std::make_shared<NumberExpr>(-number->value);
    }
    return std::make_shared<OperatorExpression>(CoreOperator::UnaryMinus,
                                                std::move(operand));
  }
  if (match(TokenId::PLUS))
    return parseUnary();
  auto result = parsePrimary();
  while (at(TokenId::DOT) || at(TokenId::COLON)) {
    const auto beforeByte = byte_;
    const auto beforePiece = piece_;
    const auto separator = input_.entry(piece_).id;
    match(separator);
    const auto separatorEnd = byte_;
    if (!atNameRange() || sourceContainsLineBreak(separatorEnd, byte_)) {
      byte_ = beforeByte;
      piece_ = beforePiece;
      break;
    }
    result =
        std::make_shared<AccessExpr>(std::move(result), consumeNameRange());
  }
  return result;
}

std::shared_ptr<Expr> IntegerParser::parseBinaryExpression(
    int minimumPrecedence, TokenId::Id stop,
    const std::vector<PatternLexeme> *stopAnchor) {
  RecursionScope recursion(*this);
  auto left = parseUnary();
  while (true) {
    step();
    skipTrivia();
    if (!input_.has(piece_) || input_.entry(piece_).begin > byte_ ||
        input_.entry(piece_).end <= byte_)
      break;
    // Both checks below must ask "is this ID standing alone here", not just
    // compare the raw SentencePiece ID -- otherwise a keyword ID that is
    // actually the leading fragment of a longer identifier on the next
    // statement (`orFilter` after `plain := 1.0`, `thenable` as a `then`
    // stop anchor, ...) gets misread as the reserved word/operator, matching
    // the same contiguity rule atNameRange()/builtinSequenceLength() already
    // apply. at() runs that same check for any ID, keyword or not.
    if (stop != TokenId::UNKNOWN && at(stop))
      break;
    if (stopAnchor && atPatternAnchor(*stopAnchor))
      break;
    const auto operatorBegin = byte_;
    if (auto pattern = tryParseTrailingPattern(left, minimumPrecedence)) {
      if (byte_ <= operatorBegin) {
        throw IntegerParserError("Trailing mixfix parser made no progress");
      }
      left = std::move(pattern);
      continue;
    }
    const auto candidateId = input_.entry(piece_).id;
    if (!at(candidateId))
      break;
    const auto definition = infixOperatorForId(candidateId);
    if (!definition ||
        static_cast<int>(definition->precedence) < minimumPrecedence)
      break;
    match(candidateId);
    const int nextMinimum =
        definition->associativity == OperatorAssociativity::Right
            ? static_cast<int>(definition->precedence)
            : static_cast<int>(definition->precedence) + 1;
    auto right = parseBinaryExpression(nextMinimum, stop, stopAnchor);
    left = std::make_shared<OperatorExpression>(definition->id, std::move(left),
                                                std::move(right));
  }
  return left;
}

std::shared_ptr<Expr> IntegerParser::parseExpressionText() {
  auto result = parseExpression();
  if (!atEnd())
    throw IntegerParserError("Unexpected source after expression");
  return result;
}

FelidaeIr IntegerParser::compileExpressionIr() {
  const auto expression = parseExpressionText();
  const auto hasMixfix = [](const auto &self,
                            const std::shared_ptr<Expr> &node) -> bool {
    if (const auto operation =
            std::dynamic_pointer_cast<OperatorExpression>(node)) {
      if (operation->coreOperator == CoreOperator::Unknown)
        return true;
      for (std::size_t index = 0; index < operation->captureCount(); ++index) {
        if (self(self, operation->capture(index)))
          return true;
      }
    } else if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(node)) {
      for (const auto &item : array->items)
        if (self(self, item))
          return true;
    } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(node)) {
      for (const auto &entry : map->entries)
        if (self(self, entry.value))
          return true;
    } else if (const auto access =
                   std::dynamic_pointer_cast<AccessExpr>(node)) {
      return self(self, access->target);
    } else if (const auto term = std::dynamic_pointer_cast<TermExpr>(node)) {
      for (const auto &argument : term->args)
        if (self(self, argument.value))
          return true;
    }
    return false;
  };
  if (hasMixfix(hasMixfix, expression)) {
    if (!mixfixModel_) {
      throw IntegerParserError("mixfix expression requires the verified "
                               "MixfixStateModel compiler backend");
    }
    return compileModelRoutedMixfixExpressionIr(expression);
  }
  return IrCodeGenerator::lowerExpression(expression);
}

FelidaeIr IntegerParser::compileModelRoutedMixfixExpressionIr(
    const std::shared_ptr<Expr> &expression) const {
  // This vocabulary is deliberately fixed-size and structural. Dynamic
  // literals/symbols are collected into bounded parser-owned tables below;
  // model output can reference them but never manufacture a machine word.
  FelidaeIr shell;
  shell.registerCount = kMaximumMixfixRegisters;
  const auto addConstant = [&](IrConstantKind kind, IrConstant value,
                               PieceSequence text = {}) {
    if (shell.constants.size() >= kMaximumMixfixReferences) {
      throw IntegerParserError(
          "mixfix compiler context has too many constants");
    }
    if (kind == IrConstantKind::Text) {
      shell.texts.push_back(std::move(text));
      value = shell.texts.size() - 1;
    }
    shell.constants.push_back({kind, value});
  };
  const auto addSymbol = [&](SymbolId symbol) {
    if (std::find(shell.symbols.begin(), shell.symbols.end(), symbol) !=
        shell.symbols.end())
      return;
    if (shell.symbols.size() >= kMaximumMixfixReferences) {
      throw IntegerParserError("mixfix compiler context has too many symbols");
    }
    shell.symbols.push_back(symbol);
  };
  const auto collect = [&](const auto &self,
                           const std::shared_ptr<Expr> &node) -> void {
    if (const auto number = std::dynamic_pointer_cast<NumberExpr>(node)) {
      addConstant(IrConstantKind::Number, encodeIrNumber(number->value));
    } else if (const auto boolean = std::dynamic_pointer_cast<BoolExpr>(node)) {
      addConstant(IrConstantKind::Boolean, boolean->value ? 1 : 0);
    } else if (std::dynamic_pointer_cast<NilExpr>(node)) {
      addConstant(IrConstantKind::Nil, 0);
    } else if (const auto text = std::dynamic_pointer_cast<StringExpr>(node)) {
      if (text->containsEscape) {
        throw IntegerParserError("string literal cannot be lowered without its "
                                 "original SentencePiece IDs");
      }
      addConstant(IrConstantKind::Text, 0, text->sentencePieceIds);
    } else if (const auto variable = std::dynamic_pointer_cast<VarExpr>(node)) {
      addSymbol(variable->nameId);
    } else if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(node)) {
      for (const auto &item : array->items)
        self(self, item);
    } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(node)) {
      if (!map->factType.empty())
        addSymbol(symbolIdForName(map->factType));
      for (const auto &entry : map->entries) {
        addSymbol(entry.keyId);
        self(self, entry.value);
      }
    } else if (const auto access =
                   std::dynamic_pointer_cast<AccessExpr>(node)) {
      self(self, access->target);
      addSymbol(access->keyId);
    } else if (const auto term = std::dynamic_pointer_cast<TermExpr>(node)) {
      addSymbol(term->nameId);
      for (const auto &argument : term->args) {
        if (!argument.name.empty())
          addSymbol(argument.nameId);
        self(self, argument.value);
      }
    } else if (const auto operation =
                   std::dynamic_pointer_cast<OperatorExpression>(node)) {
      for (std::size_t index = 0; index < operation->captureCount(); ++index)
        self(self, operation->capture(index));
    }
  };
  collect(collect, expression);
  if (const auto operation =
          std::dynamic_pointer_cast<OperatorExpression>(expression)) {
    for (const auto *overload :
         operators_->overloadsForPattern(operation->patternId)) {
      addSymbol(overload->methodId);
    }
  }

  const auto &sourceSpan = expression->sourceSpan;
  shell.sourceMap.push_back({0,
                             {sourceSpan.startLine, sourceSpan.startColumn,
                              sourceSpan.endLine, sourceSpan.endColumn}});

  MixfixContext context = makeMixfixContext(shell);

  const auto expressionBegin = sourceOffset(expression->sourceSpan.startLine,
                                            expression->sourceSpan.startColumn);
  const auto expressionEnd = sourceOffset(expression->sourceSpan.endLine,
                                          expression->sourceSpan.endColumn);
  std::size_t firstPiece = 0;
  while (input_.has(firstPiece) &&
         input_.entry(firstPiece).end <= expressionBegin)
    ++firstPiece;
  std::size_t pastLastPiece = firstPiece;
  while (input_.has(pastLastPiece) &&
         input_.entry(pastLastPiece).begin < expressionEnd) {
    ++pastLastPiece;
  }
  if (firstPiece == pastLastPiece) {
    throw IntegerParserError(
        "mixfix compiler expression has no bounded SentencePiece source span");
  }
  return compileVerifiedMixfixSpanIr(*mixfixModel_, context, std::move(shell),
                                     firstPiece, pastLastPiece);
}

FelidaeIr IntegerParser::compileVerifiedMixfixSpanIr(
    MixfixStateModel &model, const MixfixContext &context, FelidaeIr irShell,
    std::size_t firstPiece, std::size_t pastLastPiece) const {
  if (firstPiece >= pastLastPiece || !input_.has(pastLastPiece - 1)) {
    throw IntegerParserError(
        "mixfix compiler span is outside the SentencePiece input");
  }
  std::vector<SentencePieceId> ids;
  ids.reserve(pastLastPiece - firstPiece);
  for (std::size_t index = firstPiece; index < pastLastPiece; ++index) {
    if (input_.entry(index).id < 0) {
      throw IntegerParserError("SentencePiece produced a negative token ID");
    }
    ids.push_back(static_cast<SentencePieceId>(input_.entry(index).id));
  }
  const auto diagnosticSpan = irShell.sourceMap.empty()
                                  ? IrSourceMapEntry::Span{}
                                  : irShell.sourceMap.front().sourceSpan;
  try {
#ifndef NDEBUG
    if (parserTraceEnabled()) {
      std::clog << "[felidae.parser] action=compiler_ssm span_first_piece="
                << firstPiece << " span_past_last_piece=" << pastLastPiece
                << " input_ids=" << ids.size() << '\n';
    }
#endif
    auto verified =
        compileVerifiedMixfixIr(model, ids, context, std::move(irShell));
#ifndef NDEBUG
    if (parserTraceEnabled()) {
      std::clog << "[felidae.parser] action=compiler_ssm verified_ir_words="
                << verified.words.size()
                << " registers=" << verified.registerCount << '\n';
    }
#endif
    return verified;
  } catch (const IrError &error) {
    throw IntegerParserError(
        "mixfix compiler decision at " +
        std::to_string(diagnosticSpan.startLine) + ":" +
        std::to_string(diagnosticSpan.startColumn) +
        " for SentencePiece range [" + std::to_string(firstPiece) + ", " +
        std::to_string(pastLastPiece) + "): " + error.what());
  }
}

void IntegerParser::registerOperatorImplementation(const Call &annotation,
                                                   const ClauseStmt &method) {
  if (!operators_)
    return;
  const bool overload = annotation.builtinId == BuiltinId::OverloadAnnotation ||
                        annotation.name == "overload";
  const bool mixfix = annotation.builtinId == BuiltinId::MixfixAnnotation ||
                      annotation.name == "mixfix";
  if (!overload && !mixfix)
    return;
  Call normalized = annotation;
  normalized.builtinId =
      mixfix ? BuiltinId::MixfixAnnotation : BuiltinId::OverloadAnnotation;
  try {
    const auto parsed = decodeOperatorAnnotation(normalized);
    const auto *pattern =
        parsed.pattern.empty()
            ? operators_->findPatternByOperator(parsed.operatorName)
            : operators_->findPattern(parsed.operatorName, parsed.pattern);
    if (!pattern)
      throw IntegerParserError(
          "operator implementation has no registered pattern");
    operators_->registerOverload(makeOperatorOverloadDefinition(
        parsed, *pattern, method.head.name, method.head.nameId, method.module));
  } catch (const std::runtime_error &error) {
    throw IntegerParserError(error.what());
  }
}

SymbolId
IntegerParser::resolveMixfixMethod(const OperatorExpression &expression) const {
  if (!operators_)
    return 0;
  const auto expressionType = [](const std::shared_ptr<Expr> &expression) {
    if (std::dynamic_pointer_cast<NumberExpr>(expression))
      return LanguageTypeId::Number;
    if (std::dynamic_pointer_cast<StringExpr>(expression))
      return LanguageTypeId::String;
    if (std::dynamic_pointer_cast<BoolExpr>(expression))
      return LanguageTypeId::Bool;
    if (std::dynamic_pointer_cast<ArrayExpr>(expression))
      return LanguageTypeId::Array;
    if (const auto map = std::dynamic_pointer_cast<MapExpr>(expression);
        map && !map->factType.empty())
      return LanguageTypeId::Fact;
    if (const auto operation =
            std::dynamic_pointer_cast<OperatorExpression>(expression);
        operation && operation->coreOperator == CoreOperator::Unknown)
      return LanguageTypeId::Mixfix;
    if (const auto hir = std::dynamic_pointer_cast<AstValueExpr>(expression)) {
      if (hir->valueKind == AstValueKind::Statement)
        return LanguageTypeId::Stmt;
      if (hir->valueKind == AstValueKind::Statements)
        return LanguageTypeId::Statements;
      return LanguageTypeId::Expr;
    }
    if (const auto variable = std::dynamic_pointer_cast<VarExpr>(expression))
      return variable->languageTypeId;
    return LanguageTypeId::Unknown;
  };
  const auto isNumeric = [](LanguageTypeId type) {
    return type == LanguageTypeId::Number || type == LanguageTypeId::Int ||
           type == LanguageTypeId::Float || type == LanguageTypeId::Double ||
           type == LanguageTypeId::Decimal;
  };
  // A candidate's score is the sum of per-capture specificity. Equal totals
  // are a genuine ambiguity and are deliberately left for the compiler SSM.
  // In descending order: exact concrete, typed category, mixfix shape, expr,
  // and unconstrained any/unknown.
  const auto specificity = [&](LanguageTypeId wanted,
                               LanguageTypeId actual) -> std::optional<int> {
    if (wanted == LanguageTypeId::Unknown || wanted == LanguageTypeId::Any)
      return 1;
    if (wanted == LanguageTypeId::Expr)
      return 2;
    if (wanted == LanguageTypeId::Mixfix)
      return actual == LanguageTypeId::Mixfix ? std::optional<int>{3}
                                              : std::nullopt;
    if (actual == wanted)
      return 5;
    if (actual == LanguageTypeId::Unknown)
      return 4;
    if (wanted == LanguageTypeId::Number && isNumeric(actual))
      return 4;
    return std::nullopt;
  };
  std::vector<const OperatorOverloadDefinition *> matches;
  int highestSpecificity = -1;
  for (const auto *candidate :
       operators_->overloadsForPattern(expression.patternId)) {
    if (candidate->captures.size() != expression.captureCount())
      continue;
    int candidateSpecificity = 0;
    bool compatible = true;
    for (std::size_t index = 0; index < expression.captureCount(); ++index) {
      const auto wanted = candidate->captures[index].languageTypeId;
      const auto actual = expressionType(expression.capture(index));
      const auto rank = specificity(wanted, actual);
      if (!rank) {
        compatible = false;
        break;
      }
      candidateSpecificity += *rank;
    }
    if (!compatible)
      continue;
    if (candidateSpecificity > highestSpecificity) {
      highestSpecificity = candidateSpecificity;
      matches.clear();
    }
    if (candidateSpecificity == highestSpecificity)
      matches.push_back(candidate);
  }
  if (highestSpecificity < 0)
    throw IntegerParserError("mixfix expression has no compatible overload");
  return matches.size() == 1 ? matches.front()->methodId : 0;
}

SymbolId IntegerParser::resolveModelMixfixMethod(
    const std::shared_ptr<OperatorExpression> &expression) const {
  const auto ir = compileModelRoutedMixfixExpressionIr(expression);
  std::unordered_set<SymbolId> selected;
  for (std::size_t pc = 0; pc < ir.words.size();) {
    const auto opcode = static_cast<IrOpcode>(ir.words[pc]);
    if (opcode == IrOpcode::Call || opcode == IrOpcode::CallNamed) {
      const auto index = static_cast<std::size_t>(ir.words[pc + 2]);
      if (index >= ir.symbols.size())
        throw IntegerParserError(
            "mixfix model call has an invalid symbol reference");
      selected.insert(ir.symbols[index]);
    }
    try {
      pc += irInstructionWidth(ir, pc);
    } catch (const IrError &error) {
      throw IntegerParserError("mixfix model emitted invalid IR: " +
                               std::string(error.what()));
    }
  }
  if (selected.size() != 1) {
    throw IntegerParserError(
        "mixfix model must emit exactly one target procedure call");
  }
  return *selected.begin();
}

SourceSpan IntegerParser::span(std::size_t begin, std::size_t end) const {
  SourceSpan result;
  const auto [startLine, startColumn] = sourcePosition(begin);
  const auto [endLine, endColumn] = sourcePosition(end);
  result.startLine = startLine;
  result.startColumn = startColumn;
  result.endLine = endLine;
  result.endColumn = endColumn;
  return result;
}

std::pair<int, int> IntegerParser::sourcePosition(std::size_t offset) const {
  offset = std::min(offset, input_.source().size());
  const auto next =
      std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
  const auto lineIndex =
      static_cast<std::size_t>(next - lineStarts_.begin() - 1);
  return {static_cast<int>(lineIndex + 1),
          static_cast<int>(offset - lineStarts_[lineIndex] + 1)};
}

std::size_t IntegerParser::sourceOffset(int line, int column) const {
  if (line <= 0 || column <= 0 ||
      static_cast<std::size_t>(line) > lineStarts_.size()) {
    throw IntegerParserError("Source span is outside the input");
  }
  const auto offset = lineStarts_[static_cast<std::size_t>(line - 1)] +
                      static_cast<std::size_t>(column - 1);
  return std::min(offset, input_.source().size());
}

void IntegerParser::stamp(const std::shared_ptr<AstNode> &node,
                          std::size_t begin, std::size_t end) const {
  node->sourceSpan = span(begin, end);
}

} // namespace Felidae
