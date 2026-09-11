#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Felidae::Form {

// Thin ownership boundary around RocksDB. Higher VM layers own Felidae value
// encoding and index layout; this class owns only durable ordered bytes,
// atomic batches, and bounded keyset scans.
class RocksDb {
public:
  // A maximum-size VM page plus one lookahead row. Larger reads must page.
  static constexpr std::size_t maximumScanRecords = 10'001;
  struct Mutation {
    enum class Kind { Put, Erase } kind = Kind::Put;
    std::string key;
    std::string value;
  };

  struct Scan {
    std::vector<std::pair<std::string, std::string>> rows;
    bool hasMore = false;
  };

  explicit RocksDb(const std::filesystem::path &directory);
  ~RocksDb();
  RocksDb(RocksDb &&) noexcept;
  RocksDb &operator=(RocksDb &&) noexcept;
  RocksDb(const RocksDb &) = delete;
  RocksDb &operator=(const RocksDb &) = delete;

  std::optional<std::string> get(std::string_view key) const;
  std::optional<std::uint64_t> integerProperty(std::string_view name) const;
  std::string identity() const;
  void write(std::span<const Mutation> mutations);
  Scan scanPrefix(std::string_view prefix,
                  std::optional<std::string_view> after,
                  std::size_t records) const;

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace Felidae::Form
