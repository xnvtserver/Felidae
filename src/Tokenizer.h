#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Felidae {

struct EncodedToken {
  int id = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
};

// Deterministic word vocabulary. A whole identifier is looked up (and, if
// unseen, appended) as one dictionary entry; punctuation and operator bytes
// are matched against the same table by longest match.
//
// Line N in model.txt owns ID N. <0xNN> is the escaped spelling for bytes
// which cannot be represented literally in a line-oriented text file.
// Unknown identifier words are appended to this vocabulary instance in
// lexical order by prime() and are immediately reusable by encode/decode.
// Runtime parsing never rewrites the checked-in model file.
//
// Determinism holds within one vocabulary instance: once a spelling is assigned an
// ID (whether loaded from the checked-in model.txt or appended during this
// run), every later lookup of that same spelling returns that same ID for
// the rest of the run - entries_ is append-only and never reordered or
// rewritten in memory. prime() further makes a single file's newly-discovered
// words get IDs in a fixed (lexical) order, independent of incidental
// parser backtracking or statement-chunking order. What is NOT guaranteed
// is cross-instance stability of IDs assigned to words absent from model.txt.
// Nothing outside one parse reads a dynamic token's numeric value; AST names
// use the process-wide SymbolInterner instead.
class WordVocabulary {
public:
  explicit WordVocabulary(std::filesystem::path modelPath = {});

  std::vector<int> encode(std::string_view text) const;
  std::vector<EncodedToken> encodeWithOffsets(std::string_view text) const;
  std::string decode(std::span<const int> tokens) const;
  // Reserve unknown identifier words in lexical order before the parser
  // traverses the source. This makes newly assigned line IDs independent of
  // parser backtracking and statement chunking.
  void prime(std::string_view source) const;

  const std::filesystem::path &modelPath() const noexcept { return modelPath_; }

private:
  struct Entry {
    std::string spelling;
    std::string bytes;
  };

  void loadModel();
  void ensureFixedVocabulary();
  // matchOrder is for the longest-match punctuation/operator scan only
  // (encodeWithOffsets's non-word, non-digit fallback); word-byte runs and
  // digits are always looked up by exact spelling via findToken/byBytes_
  // and never reach that scan, so appending them there too would only cost
  // an O(matchOrder_ size) re-sort per new identifier for an entry that can
  // never be found again. Only the single "unrecognized punctuation byte"
  // call site passes true.
  int appendToken(std::string_view bytes, bool addToMatchOrder) const;
  int findToken(std::string_view bytes) const;
  void indexEntry(std::size_t index) const;

  std::filesystem::path modelPath_;
  mutable std::vector<Entry> entries_;
  mutable std::vector<std::pair<std::string, int>> matchOrder_;
  // findToken used to scan entries_ linearly (O(vocabulary size) per
  // identifier occurrence, every occurrence, in every program - the one
  // table it never skips). This index makes an exact-spelling lookup O(1)
  // average instead, which is the lookup encodeWithOffsets performs for
  // every identifier and digit run it sees. Keyed by the same `bytes` (the
  // decoded, actual text) findToken compares by.
  mutable std::unordered_map<std::string, int> byBytes_;
  mutable std::mutex mutex_;
};

std::filesystem::path defaultTokenizerModelPath();

} // namespace Felidae
