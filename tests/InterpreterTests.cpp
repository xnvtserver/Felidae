#include "RocksFactStore.h"

#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testOrderedRocksFactKeys() {
    using Felidae::NumberExpr;
    using Felidae::RocksFactStore;
    using Felidae::StringExpr;
    const auto numericKey = [](double value) {
        return RocksFactStore::encodeFactKey(
            "Metric", {std::make_shared<NumberExpr>(value)});
    };
    require(numericKey(-1.0) < numericKey(0.0) &&
            numericKey(0.0) < numericKey(1.0),
            "RocksDB numeric keys are not lexicographically ordered");
    require(numericKey(-0.0) == numericKey(0.0),
            "RocksDB keys distinguish negative zero from zero");

    const auto composite = RocksFactStore::encodeFactKey(
        "Place", {std::make_shared<StringExpr>("a"),
                  std::make_shared<StringExpr>("b")});
    const auto embeddedSeparator = RocksFactStore::encodeFactKey(
        "Place", {std::make_shared<StringExpr>(std::string("a\0b", 3))});
    require(composite != embeddedSeparator,
            "RocksDB composite key encoding is ambiguous");
}

void testSharedDatabaseHandleAndSerializedIds() {
    using Felidae::MapEntry;
    using Felidae::MapExpr;
    using Felidae::RocksFactStore;
    using Felidae::StringExpr;
    namespace fs = std::filesystem;

    const auto directory = fs::temp_directory_path() /
        ("felidae-shared-store-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    struct RemoveDirectory {
        fs::path path;
        ~RemoveDirectory() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{directory};
    fs::create_directories(directory);

    RocksFactStore first(directory);
    RocksFactStore second(directory);
    const auto insert = [](RocksFactStore& store, const std::string& id) {
        auto key = std::make_shared<StringExpr>(id);
        auto value = std::make_shared<MapExpr>(
            std::vector<MapEntry>{{"id", key->clone()}});
        value->factType = "ConcurrentNode";
        return store.insertFact("ConcurrentNode", {key}, value, false);
    };
    auto left = std::async(std::launch::async, [&] { return insert(first, "left"); });
    auto right = std::async(std::launch::async, [&] { return insert(second, "right"); });
    const auto leftFact = left.get();
    const auto rightFact = right.get();
    require(leftFact.id != rightFact.id,
            "Concurrent RocksDB sessions allocated the same record identity");
    require(second.findFact(
                "ConcurrentNode", {std::make_shared<StringExpr>("left")}).has_value(),
            "Second RocksDB session cannot read the first session's write");
    require(first.findFact(
                "ConcurrentNode", {std::make_shared<StringExpr>("right")}).has_value(),
            "First RocksDB session cannot read the second session's write");
}

} // namespace

int main() {
    try {
        testOrderedRocksFactKeys();
        testSharedDatabaseHandleAndSerializedIds();
        std::cout << "RocksDB storage unit tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RocksDB storage unit test failed: " << error.what() << '\n';
        return 1;
    }
}
