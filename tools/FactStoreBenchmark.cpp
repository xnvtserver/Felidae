#include "form/RegisterVm.h"
#include "form/libs/RocksDb.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

std::size_t argument(char **begin, char **end, std::string_view name,
                     std::size_t fallback) {
  for (auto current = begin; current != end; ++current) {
    if (*current != name || current + 1 == end)
      continue;
    const std::string_view value(*++current);
    std::size_t parsed = 0;
    try {
      const auto number = std::stoull(std::string(value), &parsed);
      if (parsed != value.size() || number == 0 ||
          number > std::numeric_limits<std::size_t>::max())
        throw std::invalid_argument("range");
      return static_cast<std::size_t>(number);
    } catch (const std::exception &) {
      throw Felidae::IrError(std::string(name) + " requires a positive integer");
    }
  }
  return fallback;
}

double milliseconds(std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

std::uintmax_t directoryBytes(const std::filesystem::path &directory) {
  std::uintmax_t result = 0;
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator(directory, error), end;
       !error && iterator != end; iterator.increment(error)) {
    if (iterator->is_regular_file(error))
      result += iterator->file_size(error);
  }
  if (error)
    throw Felidae::IrError("cannot measure benchmark storage: " + error.message());
  return result;
}

} // namespace

int main(int argc, char **argv) try {
  using Clock = std::chrono::steady_clock;
  const auto records = argument(argv + 1, argv + argc, "--records", 10'000);
  const auto pageSize = argument(argv + 1, argv + argc, "--page", 100);
  const auto warmIterations = argument(argv + 1, argv + argc, "--warm", 100);
  if (pageSize > 10'000)
    throw Felidae::IrError("--page exceeds the 10000 row runtime limit");

  const std::filesystem::path database =
      std::filesystem::path(FELIDAE_BENCHMARK_OUTPUT_DIR) /
      ("fact-store-" + std::to_string(records) + ".rocksdb");
  std::error_code error;
  std::filesystem::remove_all(database, error);
  if (error)
    throw Felidae::IrError("cannot clear benchmark database: " + error.message());

  const Felidae::PieceSequence typePieces{101};
  const Felidae::PieceSequence fieldPieces{102};
  const auto ingestBegin = Clock::now();
  {
    Felidae::VmFactStore store(database);
    const auto type = store.internSymbol(typePieces);
    const auto field = store.internSymbol(fieldPieces);
    store.registerType(type, {}, {{field}});
    for (std::size_t index = 0; index < records; ++index) {
      auto fact = std::make_shared<Felidae::VmFact>();
      fact->type = type;
      fact->fields = {{field, static_cast<double>(index % 100)}};
      (void)store.retain(fact);
    }
  }
  const auto ingestEnd = Clock::now();

  double reopenMilliseconds = 0.0;
  double coldMilliseconds = 0.0;
  double warmMeanMilliseconds = 0.0;
  std::size_t returnedRows = 0;
  bool hasMore = false;
  std::size_t observedRows = 0;
  {
    const auto openBegin = Clock::now();
    Felidae::VmFactStore store(database);
    const auto type = *store.findSymbol(typePieces);
    const auto field = *store.findSymbol(fieldPieces);
    const std::pair<Felidae::IrSymbolRef, Felidae::VmValue> predicate{field, 42.0};
    const auto openEnd = Clock::now();
    const auto coldBegin = Clock::now();
    const auto cold = store.pageMatching(type, {&predicate, 1}, pageSize);
    const auto coldEnd = Clock::now();
    const auto warmBegin = Clock::now();
    for (std::size_t index = 0; index < warmIterations; ++index)
      observedRows += store.pageMatching(type, {&predicate, 1}, pageSize).rows.size();
    const auto warmEnd = Clock::now();
    reopenMilliseconds = milliseconds(openEnd - openBegin);
    coldMilliseconds = milliseconds(coldEnd - coldBegin);
    warmMeanMilliseconds =
        milliseconds(warmEnd - warmBegin) / static_cast<double>(warmIterations);
    returnedRows = cold.rows.size();
    hasMore = cold.hasMore;
  }
  Felidae::Form::RocksDb metrics(database);
  const auto indexReaderBytes =
      metrics.integerProperty("rocksdb.estimate-table-readers-mem");
  const auto liveDataBytes =
      metrics.integerProperty("rocksdb.estimate-live-data-size");
  const auto sstBytes = metrics.integerProperty("rocksdb.total-sst-files-size");

  const auto ingestSeconds =
      std::chrono::duration<double>(ingestEnd - ingestBegin).count();
  std::cout << "{\n"
            << "  \"records\": " << records << ",\n"
            << "  \"requested_page_size\": " << pageSize << ",\n"
            << "  \"returned_page_size\": " << returnedRows << ",\n"
            << "  \"has_more\": " << (hasMore ? "1.0" : "0.0") << ",\n"
            << "  \"ingestion_records_per_second\": "
            << static_cast<double>(records) / ingestSeconds << ",\n"
            << "  \"reopen_milliseconds\": " << reopenMilliseconds << ",\n"
            << "  \"cold_lookup_milliseconds\": " << coldMilliseconds << ",\n"
            << "  \"warm_lookup_mean_milliseconds\": " << warmMeanMilliseconds << ",\n"
            << "  \"warm_observed_rows\": " << observedRows << ",\n"
            << "  \"index_reader_memory_bytes\": " << indexReaderBytes.value_or(0) << ",\n"
            << "  \"estimated_live_data_bytes\": " << liveDataBytes.value_or(0) << ",\n"
            << "  \"sst_storage_bytes\": " << sstBytes.value_or(0) << ",\n"
            << "  \"storage_size_bytes\": " << directoryBytes(database) << ",\n"
            << "  \"cache_state_cold\": \"database reopened; operating-system cache not flushed\",\n"
            << "  \"cache_state_warm\": \"same process and query repeated\",\n"
            << "  \"claim\": \"measured local dataset only; no trillion-scale guarantee\"\n"
            << "}\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "error: " << error.what() << '\n';
  return 1;
}
