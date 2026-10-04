#include "FelidaeRuntime.h"
#include "IntegerTokenList.h"
#include "RocksFactStore.h"

#include <rocksdb/db.h>
#include <rocksdb/options.h>

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

void testAdditiveStoreFormatUpgrade() {
    using Felidae::RocksFactStore;
    namespace fs = std::filesystem;

    const auto directory = fs::temp_directory_path() /
        ("felidae-v8-upgrade-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    struct RemoveDirectory {
        fs::path path;
        ~RemoveDirectory() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{directory};
    fs::create_directories(directory);

    const auto encodedVersion = [](std::uint64_t value) {
        std::string encoded;
        for (int shift = 56; shift >= 0; shift -= 8) {
            encoded.push_back(static_cast<char>((value >> shift) & 0xffU));
        }
        return encoded;
    };
    {
        rocksdb::Options options;
        options.create_if_missing = true;
        std::unique_ptr<rocksdb::DB> database;
        require(rocksdb::DB::Open(
                    options, directory.string(), &database).ok(),
                "Cannot create the V8 migration fixture");
        rocksdb::WriteOptions write;
        write.sync = true;
        require(database->Put(write, "Mformat", encodedVersion(8)).ok(),
                "Cannot write the V8 format marker");
        require(database->Put(write, "legacy-sentinel", "kept").ok(),
                "Cannot write the V8 sentinel");
    }

    { RocksFactStore upgraded(directory); }

    rocksdb::Options options;
    std::unique_ptr<rocksdb::DB> database;
    require(rocksdb::DB::Open(
                options, directory.string(), &database).ok(),
            "Cannot reopen the upgraded store");
    std::string version;
    require(database->Get(rocksdb::ReadOptions{}, "Mformat", &version).ok() &&
            version == encodedVersion(9),
            "V8 store format marker was not upgraded to V9");
    std::string sentinel;
    require(database->Get(
                rocksdb::ReadOptions{}, "legacy-sentinel", &sentinel).ok() &&
            sentinel == "kept",
            "V8 store data changed during the additive upgrade");
}

void testLineColumnIndex() {
    // Lines end at \n, \r\n or a lone \r; columns count bytes from the line start.
    const Felidae::IntegerTokenList list(
        std::make_shared<Felidae::ByteTokenizer>(), std::string("a\r\nb\rc\nd"));
    const auto check = [&](std::size_t offset, int line, int column) {
        const auto position = list.lineColumn(offset);
        require(position.line == line && position.column == column,
                "IntegerTokenList::lineColumn reports the wrong position");
    };
    check(0, 1, 1);
    check(3, 2, 1);
    check(5, 3, 1);
    check(7, 4, 1);
    check(100, 4, 2);
}

void testLargeSourceParses() {
    // 30000 facts need about three million parser iterations. The iteration
    // budget is per statement, not per file, so a large data file must load;
    // span lookup must also stay cheap or this test exceeds its timeout.
    constexpr int kFacts = 30000;
    std::string source;
    for (int index = 0; index < kFacts; ++index) {
        source += "Item(id: \"i" + std::to_string(index) +
                  "\", n: " + std::to_string(index) + ").\n";
    }
    const auto program = Felidae::parseProgramText(source);
    require(program.statements.size() == static_cast<std::size_t>(kFacts),
            "A large source did not parse completely");
    require(program.statements.back()->sourceSpan.startLine == kFacts,
            "A large source reports the wrong line for its last statement");
}

} // namespace

int main() {
    try {
        testOrderedRocksFactKeys();
        testSharedDatabaseHandleAndSerializedIds();
        testAdditiveStoreFormatUpgrade();
        testLineColumnIndex();
        testLargeSourceParses();
        std::cout << "RocksDB storage and parser unit tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RocksDB storage unit test failed: " << error.what() << '\n';
        return 1;
    }
}
