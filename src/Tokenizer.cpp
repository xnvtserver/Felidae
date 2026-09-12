#include "Tokenizer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Felidae {
namespace {

std::string decodeModelSpelling(std::string_view spelling) {
  std::string result;
  for (std::size_t index = 0; index < spelling.size();) {
    if (index + 6 == spelling.size() && spelling[index] == '<' &&
        spelling[index + 1] == '0' && spelling[index + 2] == 'x' &&
        spelling[index + 5] == '>') {
      const auto hex = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
      };
      const int high = hex(spelling[index + 3]);
      const int low = hex(spelling[index + 4]);
      if (high >= 0 && low >= 0) {
        result.push_back(static_cast<char>((high << 4) | low));
        index += 6;
        continue;
      }
    }
    result.push_back(spelling[index++]);
  }
  return result;
}

std::string encodeModelSpelling(std::string_view bytes) {
  std::ostringstream output;
  for (const unsigned char value : bytes) {
    if (value >= 0x20 && value != 0x7f && value != '<' && value != '>')
      output << static_cast<char>(value);
    else
      output << "<0x" << std::uppercase << std::hex << std::setw(2)
             << std::setfill('0') << static_cast<unsigned>(value) << ">"
             << std::dec;
  }
  return output.str();
}

bool isWordByte(unsigned char value) {
  return value >= 0x80 || std::isalnum(value) != 0 || value == '_';
}

const std::array<std::string_view, 59> &fixedVocabulary() {
  static constexpr std::array<std::string_view, 59> values{
      "<unk>", "import", "not", "and", "or", "then", "as", "if",
      "else", "return", "where", "extend", "lambda", "true", "false",
      "nil", "(", ")", "{", "}", "[", "]", ",", ":", ".", "|", "?",
      "@", ":=", "::", "=>", "+", "-", "*", "/", "%", "==", "!=", "<",
      "<=", ">", ">=", "\"", "\\", "#", "<0x20>", "<0x09>", "<0x0A>",
      "<0x0D>", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
  return values;
}

} // namespace

std::filesystem::path defaultTokenizerModelPath() {
#ifdef FELIDAE_TOKENIZER_MODEL_PATH
  return std::filesystem::path(FELIDAE_TOKENIZER_MODEL_PATH);
#else
  return std::filesystem::path("models") / "felidae-bpe" / "model.txt";
#endif
}

WordVocabulary::WordVocabulary(std::filesystem::path modelPath)
    : modelPath_(modelPath.empty() ? defaultTokenizerModelPath()
                                   : std::move(modelPath)) {
  loadModel();
}

void WordVocabulary::loadModel() {
  std::lock_guard lock(mutex_);
  entries_.clear();
  matchOrder_.clear();
  byBytes_.clear();
  std::ifstream input(modelPath_, std::ios::binary);
  if (input) {
    std::string line;
    while (std::getline(input, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      entries_.push_back(Entry{line, decodeModelSpelling(line)});
    }
  }
  ensureFixedVocabulary();
}

void WordVocabulary::indexEntry(std::size_t index) const {
  // Skips index 0 (<unk>): its bytes are empty and every other entry's
  // absence from byBytes_ is exactly what "unknown" already means, so an
  // empty-string key would only ever be a wrong hit for genuinely-empty
  // input, never a real spelling.
  if (index == 0 || entries_[index].bytes.empty()) return;
  byBytes_.emplace(entries_[index].bytes, static_cast<int>(index));
}

void WordVocabulary::ensureFixedVocabulary() {
  const auto &fixed = fixedVocabulary();
  if (entries_.empty()) {
    entries_.reserve(fixed.size());
    for (const auto spelling : fixed)
      entries_.push_back(
          Entry{std::string(spelling), decodeModelSpelling(spelling)});
    std::error_code error;
    std::filesystem::create_directories(modelPath_.parent_path(), error);
    std::ofstream output(modelPath_, std::ios::binary | std::ios::trunc);
    if (!output)
      throw std::runtime_error("Unable to create tokenizer model: " +
                               modelPath_.string());
    for (const auto &entry : entries_) output << entry.spelling << '\n';
  } else if (entries_.size() < fixed.size()) {
    throw std::runtime_error("Tokenizer model is missing fixed syntax IDs: " +
                             modelPath_.string());
  }
  for (std::size_t index = 0; index < fixed.size(); ++index) {
    if (entries_[index].bytes != decodeModelSpelling(fixed[index]))
      throw std::runtime_error("Tokenizer model fixed ID mismatch at line " +
                               std::to_string(index) + ": " +
                               modelPath_.string());
  }
  byBytes_.reserve(entries_.size());
  for (std::size_t index = 1; index < entries_.size(); ++index) {
    indexEntry(index);
    if (!entries_[index].bytes.empty())
      matchOrder_.emplace_back(entries_[index].bytes,
                               static_cast<int>(index));
  }
  std::stable_sort(matchOrder_.begin(), matchOrder_.end(),
                   [](const auto &left, const auto &right) {
                     if (left.first.size() != right.first.size())
                       return left.first.size() > right.first.size();
                     return left.second < right.second;
                   });
}

int WordVocabulary::findToken(std::string_view bytes) const {
  // O(1) average via byBytes_ instead of a linear scan over every entry:
  // this runs once per identifier/digit occurrence encoded, in every
  // program, against a vocabulary that only ever grows (700+ entries
  // already and counting), so a full scan here was the one lookup every
  // encode call paid for unconditionally.
  const auto found = byBytes_.find(std::string(bytes));
  return found == byBytes_.end() ? -1 : found->second;
}

void WordVocabulary::appendModelLine(std::string_view bytes) const {
  std::error_code error;
  std::filesystem::create_directories(modelPath_.parent_path(), error);
  std::ofstream output(modelPath_, std::ios::binary | std::ios::app);
  if (!output)
    throw std::runtime_error("Unable to append tokenizer model: " +
                             modelPath_.string());
  output << encodeModelSpelling(bytes) << '\n';
}

int WordVocabulary::appendToken(std::string_view bytes, bool addToMatchOrder) const {
  std::lock_guard lock(mutex_);
  const int existing = findToken(bytes);
  if (existing >= 0) return existing;
  const int id = static_cast<int>(entries_.size());
  std::string owned(bytes);
  appendModelLine(owned);
  entries_.push_back(Entry{encodeModelSpelling(owned), std::move(owned)});
  indexEntry(static_cast<std::size_t>(id));
  if (addToMatchOrder) {
    matchOrder_.emplace_back(entries_.back().bytes, id);
    std::stable_sort(matchOrder_.begin(), matchOrder_.end(),
                     [](const auto &left, const auto &right) {
                       if (left.first.size() != right.first.size())
                         return left.first.size() > right.first.size();
                       return left.second < right.second;
                     });
  }
  return id;
}

void WordVocabulary::prime(std::string_view source) const {
  std::vector<std::string> words;
  for (std::size_t begin = 0; begin < source.size();) {
    if (std::isdigit(static_cast<unsigned char>(source[begin]))) {
      while (begin < source.size() &&
             std::isdigit(static_cast<unsigned char>(source[begin])))
        ++begin;
      continue;
    }
    if (!isWordByte(static_cast<unsigned char>(source[begin]))) {
      ++begin;
      continue;
    }
    std::size_t end = begin + 1;
    while (end < source.size() &&
           isWordByte(static_cast<unsigned char>(source[end])))
      ++end;
    words.emplace_back(source.substr(begin, end - begin));
    begin = end;
  }
  std::sort(words.begin(), words.end());
  words.erase(std::unique(words.begin(), words.end()), words.end());
  for (const auto &word : words) {
    bool known = false;
    {
      std::lock_guard lock(mutex_);
      known = findToken(word) >= 0;
    }
    if (!known) appendToken(word, false);
  }
}

std::vector<EncodedToken>
WordVocabulary::encodeWithOffsets(std::string_view text) const {
  std::vector<EncodedToken> result;
  std::size_t offset = 0;
  while (offset < text.size()) {
    if (std::isdigit(static_cast<unsigned char>(text[offset]))) {
      const std::string_view digit = text.substr(offset, 1);
      int id = -1;
      {
        std::lock_guard lock(mutex_);
        id = findToken(digit);
      }
      if (id < 0) id = appendToken(digit, false);
      result.push_back({id, offset, offset + 1});
      ++offset;
      continue;
    }
    if (isWordByte(static_cast<unsigned char>(text[offset]))) {
      std::size_t end = offset + 1;
      while (end < text.size() &&
             isWordByte(static_cast<unsigned char>(text[end])))
        ++end;
      const auto word = text.substr(offset, end - offset);
      int id = -1;
      {
        std::lock_guard lock(mutex_);
        id = findToken(word);
      }
      if (id < 0) id = appendToken(word, false);
      result.push_back({id, offset, end});
      offset = end;
      continue;
    }
    int id = -1;
    std::size_t length = 0;
    {
      std::lock_guard lock(mutex_);
      for (const auto &[bytes, candidate] : matchOrder_) {
        if (bytes.size() <= length || bytes.size() > text.size() - offset)
          continue;
        if (text.compare(offset, bytes.size(), bytes) == 0) {
          id = candidate;
          length = bytes.size();
          break;
        }
      }
    }
    if (id < 0) {
      id = appendToken(text.substr(offset, 1), true);
      length = 1;
    }
    result.push_back({id, offset, offset + length});
    offset += length;
  }
  return result;
}

std::vector<int> WordVocabulary::encode(std::string_view text) const {
  const auto encoded = encodeWithOffsets(text);
  std::vector<int> result;
  result.reserve(encoded.size());
  for (const auto token : encoded) result.push_back(token.id);
  return result;
}

std::string WordVocabulary::decode(std::span<const int> tokens) const {
  std::lock_guard lock(mutex_);
  std::string result;
  for (const int id : tokens) {
    if (id < 0 || static_cast<std::size_t>(id) >= entries_.size())
      throw std::runtime_error("word vocabulary decode received an invalid token ID");
    result += entries_[static_cast<std::size_t>(id)].bytes;
  }
  return result;
}

} // namespace Felidae
