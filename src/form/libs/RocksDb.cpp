#include "form/libs/RocksDb.h"

#include "form/IrModule.h"

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>

#include <system_error>

namespace Felidae::Form {
namespace {

[[noreturn]] void fail(std::string_view operation,
                       const rocksdb::Status &status) {
  throw IrError("RocksDB " + std::string(operation) + " failed: " +
                status.ToString());
}

bool hasPrefix(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

class RocksDb::Implementation {
public:
  std::unique_ptr<rocksdb::DB> database;
};

RocksDb::RocksDb(const std::filesystem::path &directory)
    : implementation_(std::make_unique<Implementation>()) {
  if (directory.empty())
    throw IrError("RocksDB directory must not be empty");
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error)
    throw IrError("cannot create RocksDB directory: " + error.message());

  rocksdb::Options options;
  options.create_if_missing = true;
  rocksdb::DB *database = nullptr;
  const auto status = rocksdb::DB::Open(options, directory.string(), &database);
  if (!status.ok())
    fail("open", status);
  implementation_->database.reset(database);
}

RocksDb::~RocksDb() = default;
RocksDb::RocksDb(RocksDb &&) noexcept = default;
RocksDb &RocksDb::operator=(RocksDb &&) noexcept = default;

std::string RocksDb::identity() const {
  std::string result;
  const auto status = implementation_->database->GetDbIdentity(result);
  if (!status.ok())
    fail("identity", status);
  return result;
}

std::optional<std::string> RocksDb::get(std::string_view key) const {
  if (key.empty())
    throw IrError("RocksDB key must not be empty");
  std::string value;
  const auto status = implementation_->database->Get(
      rocksdb::ReadOptions{}, rocksdb::Slice(key.data(), key.size()), &value);
  if (status.IsNotFound())
    return std::nullopt;
  if (!status.ok())
    fail("read", status);
  return value;
}

std::optional<std::uint64_t>
RocksDb::integerProperty(std::string_view name) const {
  if (name.empty())
    throw IrError("RocksDB property name must not be empty");
  std::uint64_t value = 0;
  if (!implementation_->database->GetIntProperty(
          rocksdb::Slice(name.data(), name.size()), &value))
    return std::nullopt;
  return value;
}

void RocksDb::write(std::span<const Mutation> mutations) {
  if (mutations.empty())
    return;
  rocksdb::WriteBatch batch;
  for (const auto &mutation : mutations) {
    if (mutation.key.empty())
      throw IrError("RocksDB mutation key must not be empty");
    rocksdb::Status status;
    if (mutation.kind == Mutation::Kind::Put) {
      status = batch.Put(mutation.key, mutation.value);
    } else {
      status = batch.Delete(mutation.key);
    }
    if (!status.ok())
      fail("batch construction", status);
  }
  rocksdb::WriteOptions options;
  options.sync = true;
  const auto status = implementation_->database->Write(options, &batch);
  if (!status.ok())
    fail("batch commit", status);
}

RocksDb::Scan RocksDb::scanPrefix(
    std::string_view prefix, std::optional<std::string_view> after,
    std::size_t records) const {
  if (prefix.empty())
    throw IrError("RocksDB scan prefix must not be empty");
  if (after && !hasPrefix(*after, prefix))
    throw IrError("RocksDB cursor does not belong to the requested prefix");
  if (records > maximumScanRecords)
    throw IrError("RocksDB scan record limit is too large");

  rocksdb::ReadOptions options;
  options.total_order_seek = false;
  std::unique_ptr<rocksdb::Iterator> iterator(
      implementation_->database->NewIterator(options));
  const auto seek = after.value_or(prefix);
  iterator->Seek(rocksdb::Slice(seek.data(), seek.size()));
  if (after && iterator->Valid() && iterator->key().ToStringView() == *after)
    iterator->Next();

  Scan result;
  result.rows.reserve(records);
  while (iterator->Valid() &&
         hasPrefix(iterator->key().ToStringView(), prefix)) {
    if (result.rows.size() == records) {
      result.hasMore = true;
      break;
    }
    result.rows.emplace_back(iterator->key().ToString(),
                             iterator->value().ToString());
    iterator->Next();
  }
  if (!iterator->status().ok())
    fail("scan", iterator->status());
  return result;
}

} // namespace Felidae::Form
