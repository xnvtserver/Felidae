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

// Deterministic, training-free word vocabulary - not byte-pair encoding.
// Despite the class's former name, no byte-pair merging ever happens here:
// a whole identifier is looked up (and, if unseen, appended) as one atomic
// dictionary entry, and non-word punctuation/operator bytes are matched
// against the same table by longest match. That is exactly what
// README.md/code.md already describe ("unknown words are added
// deterministically in source order", "execution needs no training") - the
// prior name just kept "Bpe" from this project's earlier, removed
// SentencePiece-adjacent tokenizer despite no longer describing what this
// class actually does.
//
// Line N in model.txt owns ID N. <0xNN> is the escaped spelling for bytes
// which cannot be represented literally in a line-oriented text file.
// Unknown identifier words are appended in encounter order and are
// immediately reusable by encode/decode.
//
// Determinism holds within one process run: once a spelling is assigned an
// ID (whether loaded from the checked-in model.txt or appended during this
// run), every later lookup of that same spelling returns that same ID for
// the rest of the run - entries_ is append-only and never reordered or
// rewritten. prime() further makes a single file's own newly-discovered
// words get IDs in a fixed (lexical) order, independent of incidental
// parser backtracking or statement-chunking order. What is NOT guaranteed
// is cross-run stability of IDs assigned to words that are genuinely new to
// this checkout's model.txt: two runs (or two machines) that first meet
// different not-yet-vocabulary words in a different order will grow the
// checked-in file differently. That is a property of any incrementally
// learned, checked-in vocabulary, not a correctness bug in a single run -
// nothing here reads a dynamically assigned ID's numeric value, only its
// identity/equality.
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
  void appendModelLine(std::string_view bytes) const;
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
