#include "form/libs/RocksDb.h"
#include "form/libs/FactStorageCodec.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

int main() {
  namespace fs = std::filesystem;
  using Felidae::Form::RocksDb;

  // Corrupt CBOR numbers must not wrap/truncate into valid IDs or tags.
  {
    using Json = nlohmann::ordered_json;
    Felidae::VmFact fact;
    fact.type = 1;
    fact.fields = {{1, 3.5}};
    const std::vector<Felidae::PieceSequence> symbols{{11}};
    const auto bytes = Felidae::Form::encodeStoredFact(fact, std::nullopt, symbols);
    const auto original = Json::from_cbor(bytes);
    const auto rejectsRecord = [&](const Json &record) {
      const auto encoded = Json::to_cbor(record);
      try {
        (void)Felidae::Form::decodeStoredFact(
            std::string_view(reinterpret_cast<const char *>(encoded.data()), encoded.size()),
            [](Felidae::PieceSequence) { return Felidae::IrSymbolRef{1}; });
      } catch (const Felidae::IrError &) { return true; }
      return false;
    };
    for (const Json invalid : {Json(-1), Json(1.5), Json(true)}) {
      for (const auto field : {0, 2, 3}) {
        auto record = original;
        record[1][field] = invalid;
        assert(rejectsRecord(record));
      }
    }
    auto record = original;
    record[1][2] = 256;
    assert(rejectsRecord(record));
    record = original;
    record[1][4][0][1][0] = original[1][4][0][1][0].get<unsigned>() + 256;
    assert(rejectsRecord(record));
    record = original;
    record[0] = 1.5;
    assert(rejectsRecord(record));
  }

  // Reopening may intern symbols in another order. Equivalent nested fact
  // keys must remain byte-for-byte identical across those runtime mappings.
  {
    const std::vector<Felidae::PieceSequence> first{{90}, {10}, {20}};
    const std::vector<Felidae::PieceSequence> second{{20}, {90}, {10}};
    auto left = std::make_shared<Felidae::VmFact>();
    left->type = 1;
    left->fields = {{2, 3.5}, {3, Felidae::VmSymbol{1}}};
    auto right = std::make_shared<Felidae::VmFact>();
    right->type = 2;
    right->fields = {{1, Felidae::VmSymbol{2}}, {3, 3.5}};
    assert(Felidae::Form::encodeStoredValueKey(Felidae::VmFactPtr{left}, first) ==
           Felidae::Form::encodeStoredValueKey(Felidae::VmFactPtr{right}, second));
    auto leftMap = std::make_shared<Felidae::VmMap>();
    leftMap->entries = left->fields;
    auto rightMap = std::make_shared<Felidae::VmMap>();
    rightMap->entries = right->fields;
    assert(Felidae::Form::encodeStoredValueKey(leftMap, first) ==
           Felidae::Form::encodeStoredValueKey(rightMap, second));
  }

  const fs::path database =
      fs::path(FELIDAE_TEST_OUTPUT_DIR) / "ordered-driver.rocksdb";
  std::error_code error;
  fs::remove_all(database, error);
  assert(!error);

  {
    RocksDb store(database);
    const std::vector<Felidae::PieceSequence> symbols{{11}, {12, 13}, {14}};
    auto nested = std::make_shared<Felidae::VmFact>();
    nested->type = 1;
    nested->fields = {{2, Felidae::VmText{{21, 22}}}};
    Felidae::VmFact fact;
    fact.id = 42;
    fact.type = 1;
    fact.createdSequence = 7;
    fact.origin = Felidae::VmFact::Origin::Derived;
    auto values = std::make_shared<Felidae::VmArray>();
    auto emptyKeyMap = std::make_shared<Felidae::VmTextMap>();
    emptyKeyMap->entries = {{Felidae::PieceSequence{}, Felidae::VmText{}}};
    values->values = {-212.421, Felidae::VmDegree(0.75),
                      Felidae::VmFactPtr{nested}, Felidae::VmText{}, emptyKeyMap};
    fact.fields = {{2, values}, {3, Felidae::VmSymbol{1}}};
    const auto encoded = Felidae::Form::encodeStoredFact(
        fact, std::string_view("facts/source.fx"), symbols);
    const std::vector<Felidae::IrSymbolRef> parents{3};
    const std::vector<std::vector<Felidae::IrSymbolRef>> indexes{{2}, {2, 3}};
    const auto encodedType = Felidae::Form::encodeStoredFactType(
        1, parents, indexes, symbols);
    const std::vector<RocksDb::Mutation> initial{
        {RocksDb::Mutation::Kind::Put, "fact/type/A/0001", "one"},
        {RocksDb::Mutation::Kind::Put, "fact/type/A/0002", "two"},
        {RocksDb::Mutation::Kind::Put, "fact/type/A/0003", "three"},
        {RocksDb::Mutation::Kind::Put, "fact/type/B/0001", "other"},
        {RocksDb::Mutation::Kind::Put, "record/00000042", encoded},
        {RocksDb::Mutation::Kind::Put, "type/primary", encodedType},
    };
    store.write(initial);
    assert(store.get("fact/type/A/0002") == "two");
    assert(!store.get("fact/type/A/missing"));
    bool oversizedScanRejected = false;
    try {
      (void)store.scanPrefix("fact/type/A/", std::nullopt,
                            RocksDb::maximumScanRecords + 1);
    } catch (const Felidae::IrError &) { oversizedScanRejected = true; }
    assert(oversizedScanRejected);
    const auto zero = store.scanPrefix("fact/type/A/", std::nullopt, 0);
    assert(zero.rows.empty() && zero.hasMore);
    const auto maximum = store.scanPrefix("fact/type/A/", std::nullopt,
                                         RocksDb::maximumScanRecords);
    assert(maximum.rows.size() == 3 && !maximum.hasMore);

    const auto first = store.scanPrefix("fact/type/A/", std::nullopt, 2);
    assert(first.rows.size() == 2 && first.hasMore);
    assert(first.rows[0].second == "one" && first.rows[1].second == "two");
    const auto second = store.scanPrefix(
        "fact/type/A/", first.rows.back().first, 2);
    assert(second.rows.size() == 1 && !second.hasMore);
    assert(second.rows.front().second == "three");

    const std::vector<RocksDb::Mutation> replacement{
        {RocksDb::Mutation::Kind::Put, "fact/type/A/0002", "updated"},
        {RocksDb::Mutation::Kind::Erase, "fact/type/A/0001", {}},
    };
    store.write(replacement);
  }

  // Closing and reopening proves this is durable storage, not an in-memory
  // test double hidden behind the driver interface.
  {
    RocksDb reopened(database);
    assert(!reopened.get("fact/type/A/0001"));
    assert(reopened.get("fact/type/A/0002") == "updated");
    const auto rows = reopened.scanPrefix("fact/type/A/", std::nullopt, 8);
    assert(rows.rows.size() == 2 && !rows.hasMore);
    std::map<Felidae::PieceSequence, Felidae::IrSymbolRef> interned;
    const auto decoded = Felidae::Form::decodeStoredFact(
        *reopened.get("record/00000042"),
        [&](Felidae::PieceSequence pieces) {
          const auto [found, _] = interned.emplace(
              std::move(pieces),
              static_cast<Felidae::IrSymbolRef>(interned.size() + 1));
          return found->second;
        });
    assert(decoded.source == "facts/source.fx");
    assert(decoded.fact && decoded.fact->id == 42 &&
           decoded.fact->createdSequence == 7 &&
           decoded.fact->origin == Felidae::VmFact::Origin::Derived);
    const auto decodedValues =
        std::get<Felidae::VmArrayPtr>(decoded.fact->fields.front().second);
    assert(decodedValues && decodedValues->values.size() == 5);
    assert(std::get<double>(decodedValues->values[0]) == -212.421);
    assert(std::get<Felidae::VmDegree>(decodedValues->values[1]).value == 0.75);
    assert(std::get<Felidae::VmFactPtr>(decodedValues->values[2]));
    assert(std::get<Felidae::VmText>(decodedValues->values[3]).pieces.empty());
    const auto emptyMap =
        std::get<Felidae::VmTextMapPtr>(decodedValues->values[4]);
    assert(emptyMap && emptyMap->entries.size() == 1);
    assert(emptyMap->entries.front().first.empty());
    assert(std::get<Felidae::VmText>(emptyMap->entries.front().second)
               .pieces.empty());
    const auto decodedType = Felidae::Form::decodeStoredFactType(
        *reopened.get("type/primary"),
        [&](Felidae::PieceSequence pieces) {
          const auto [found, _] = interned.emplace(
              std::move(pieces),
              static_cast<Felidae::IrSymbolRef>(interned.size() + 1));
          return found->second;
        });
    assert(decodedType.type != 0 && decodedType.parents.size() == 1);
    assert(decodedType.indexes.size() == 2 &&
           decodedType.indexes[1].size() == 2);
  }

  const fs::path factDatabase =
      fs::path(FELIDAE_TEST_OUTPUT_DIR) / "vm-facts.rocksdb";
  fs::remove_all(factDatabase, error);
  assert(!error);
  Felidae::IrFactRef storedId = 0;
  {
    Felidae::VmFactStore facts(factDatabase);
    const auto type = facts.internSymbol({101});
    const auto field = facts.internSymbol({102});
    facts.registerType(type, {}, {{field}});
    auto builder = std::make_shared<Felidae::VmFact>();
    builder->type = type;
    builder->fields = {{field, -212.421}};
    auto retained = facts.retain(builder);
    storedId = retained->id;
    facts.recordSource(storedId, "facts/persistent.fx");
    retained = facts.mutate(retained, field, 89.024, 0);
    assert(std::get<double>(retained->fields.front().second) == 89.024);
  }
  {
    RocksDb raw(factDatabase);
    // Type registration, insertion, ownership, and update each commit once.
    assert(raw.get("M/revision") == std::string("\0\0\0\0\0\0\0\4", 8));
    assert(raw.scanPrefix(std::string_view{"IT", 2}, std::nullopt, 8)
               .rows.size() == 1);
    assert(raw.scanPrefix(std::string_view{"IF", 2}, std::nullopt, 8)
               .rows.size() == 1);
    assert(raw.scanPrefix(std::string_view{"IS", 2}, std::nullopt, 8)
               .rows.size() == 1);
    assert(raw.scanPrefix(std::string_view{"IE", 2}, std::nullopt, 8)
               .rows.size() == 1);
  }
  {
    Felidae::VmFactStore reopened(factDatabase);
    const auto type = reopened.findSymbol(std::array<Felidae::PieceId, 1>{101});
    const auto field = reopened.findSymbol(std::array<Felidae::PieceId, 1>{102});
    assert(type && field);
    const std::array<std::pair<Felidae::IrSymbolRef, Felidae::VmValue>, 1>
        predicate{{{*field, 89.024}}};
    const auto rows = reopened.snapshotMatching(*type, predicate);
    assert(rows.size() == 1 && rows.front()->id == storedId);
    assert(reopened.sourceOf(storedId) == "facts/persistent.fx");
    const auto updated = reopened.mutate(rows.front(), *field, -3.0, 0);
    assert(std::get<double>(updated->fields.front().second) == -3.0);
    bool staleRejected = false;
    try {
      (void)reopened.mutate(rows.front(), *field, 5.0, 0);
    } catch (const Felidae::IrError &) {
      staleRejected = true;
    }
    assert(staleRejected);
    assert(reopened.erase(std::array<Felidae::VmFactPtr, 1>{updated}) == 1);
  }
  {
    Felidae::VmFactStore reopened(factDatabase);
    assert(reopened.size() == 0);
    auto replacement = std::make_shared<Felidae::VmFact>();
    replacement->type = reopened.internSymbol({101});
    replacement->fields = {{reopened.internSymbol({102}), 7.0}};
    const auto retained = reopened.retain(replacement);
    assert(retained->id > storedId);
    assert(retained->createdSequence > 1);
  }
  {
    RocksDb raw(factDatabase);
    // Reopening and the rejected stale update must not advance the revision.
    // The successful update, deletion, and replacement add three commits.
    assert(raw.get("M/revision") == std::string("\0\0\0\0\0\0\0\7", 8));
    const std::array<RocksDb::Mutation, 1> malformed{{
        {RocksDb::Mutation::Kind::Put, "M/revision", "invalid"}}};
    raw.write(malformed);
  }
  bool malformedRevisionRejected = false;
  try {
    Felidae::VmFactStore reopened(factDatabase);
  } catch (const Felidae::IrError &) {
    malformedRevisionRejected = true;
  }
  assert(malformedRevisionRejected);

  const fs::path pageDatabase =
      fs::path(FELIDAE_TEST_OUTPUT_DIR) / "paged-facts.rocksdb";
  fs::remove_all(pageDatabase, error);
  assert(!error);
  std::optional<Felidae::VmFactStore::Cursor> cursor;
  std::vector<Felidae::IrFactRef> expected;
  {
    Felidae::VmFactStore store(pageDatabase);
    const auto parent = store.internSymbol({201});
    const auto child = store.internSymbol({202});
    const auto field = store.internSymbol({203});
    const auto residual = store.internSymbol({204});
    store.registerType(parent, {}, {{field}});
    store.registerType(child, {parent});
    for (int i = 0; i < 5; ++i) {
      auto fact = std::make_shared<Felidae::VmFact>();
      fact->type = i % 2 ? child : parent;
      fact->fields = {{field, 7.0}, {residual, i % 2 ? 0.0 : 1.0}};
      expected.push_back(store.retain(fact)->id);
    }
    const auto zero = store.pageAssignableTo(parent, 0);
    assert(zero.rows.empty() && zero.hasMore);
    const auto first = store.pageAssignableTo(parent, 2);
    assert(first.rows.size() == 2 && first.hasMore && first.next);
    assert(first.rows[0]->id == expected[0]);
    assert(first.rows[1]->id == expected[1]);
    cursor = first.next;
    const auto rejects = [](const auto &action) {
      try { action(); } catch (const Felidae::IrError &) { return true; }
      return false;
    };
    assert(rejects([&] { (void)store.pageAssignableTo(child, 2, cursor); }));
    auto foreign = cursor;
    foreign->database += "other";
    assert(rejects([&] { (void)store.pageAssignableTo(parent, 2, foreign); }));
    auto invalid = cursor;
    invalid->lastKey.pop_back();
    assert(rejects([&] { (void)store.pageAssignableTo(parent, 2, invalid); }));
    assert(rejects([&] { (void)store.pageAssignableTo(parent, 10'001); }));
    std::vector<std::pair<Felidae::IrSymbolRef, Felidae::VmValue>> predicates{
        {field, 7.0}, {residual, 1.0}};
    const auto matched = store.pageMatching(parent, predicates, 2);
    assert(matched.rows.size() == 2 && matched.next && matched.hasMore);
    assert(matched.rows[0]->id == expected[0]);
    assert(matched.rows[1]->id == expected[2]);
    // Equivalent argument order must retain the same cursor query identity.
    std::reverse(predicates.begin(), predicates.end());
    const auto remaining = store.pageMatching(parent, predicates, 2, matched.next);
    assert(remaining.rows.size() == 1 && !remaining.hasMore);
    assert(remaining.rows[0]->id == expected[4]);
    predicates[0].second = 0.0;
    assert(rejects([&] {
      (void)store.pageMatching(parent, predicates, 2, matched.next);
    }));
    assert(store.pageMatching(child, predicates, 2).rows.size() == 2);
    predicates[1].second = 8.0;
    assert(store.pageMatching(parent, predicates, 2).rows.empty());
  }
  {
    Felidae::VmFactStore store(pageDatabase);
    const auto parent = store.internSymbol({201});
    const auto second = store.pageAssignableTo(parent, 2, cursor);
    assert(second.rows.size() == 2 && second.hasMore && second.next);
    assert(second.rows[0]->id == expected[2]);
    assert(second.rows[1]->id == expected[3]);
    const auto last = store.pageAssignableTo(parent, 2, second.next);
    assert(last.rows.size() == 1 && !last.hasMore && !last.next);
    assert(last.rows[0]->id == expected[4]);
    store.erase(last.rows);
    bool stale = false;
    try { (void)store.pageAssignableTo(parent, 2, cursor); }
    catch (const Felidae::IrError &) { stale = true; }
    assert(stale);
  }

  const fs::path inheritedDatabase =
      fs::path(FELIDAE_TEST_OUTPUT_DIR) / "inherited-index-pages.rocksdb";
  fs::remove_all(inheritedDatabase, error);
  assert(!error);
  std::optional<Felidae::VmFactStore::Cursor> inheritedCursor;
  Felidae::IrSymbolRef previousChild = 0;
  {
    Felidae::VmFactStore store(inheritedDatabase);
    // Deliberately intern out of canonical storage order.
    const auto child = store.internSymbol({901});
    previousChild = child;
    const auto first = store.internSymbol({801});
    const auto second = store.internSymbol({701});
    const auto field = store.internSymbol({601});
    store.registerType(first, {}, {{field}});
    store.registerType(second, {}, {{field}});
    store.registerType(child, {first, second});
    for (int i = 0; i < 3; ++i) {
      auto fact = std::make_shared<Felidae::VmFact>();
      fact->type = child;
      fact->fields = {{field, 5.0}};
      (void)store.retain(fact);
    }
    const std::array<std::pair<Felidae::IrSymbolRef, Felidae::VmValue>, 1>
        predicates{{{field, 5.0}}};
    const auto page = store.pageMatching(child, predicates, 1);
    assert(page.rows.size() == 1 && page.next && page.hasMore);
    inheritedCursor = page.next;
  }
  {
    Felidae::VmFactStore store(inheritedDatabase);
    const auto child = store.internSymbol({901});
    const auto field = store.internSymbol({601});
    assert(child != previousChild);
    const std::array<std::pair<Felidae::IrSymbolRef, Felidae::VmValue>, 1>
        predicates{{{field, 5.0}}};
    const auto next = store.pageMatching(child, predicates, 2, inheritedCursor);
    assert(next.rows.size() == 2 && !next.hasMore);
    assert(next.rows[0]->id == 2 && next.rows[1]->id == 3);
  }
  {
    RocksDb raw(inheritedDatabase);
    const std::string firstKey("F\0\0\0\0\0\0\0\1", 9);
    const auto record = raw.get(firstKey);
    assert(record);
    const std::array<RocksDb::Mutation, 1> misplaced{{
        {RocksDb::Mutation::Kind::Put,
         std::string("F\0\0\0\0\0\0\0\11", 9), *record}}};
    raw.write(misplaced);
  }
  bool misplacedRecordRejected = false;
  try {
    Felidae::VmFactStore store(inheritedDatabase);
    (void)store.snapshot();
  }
  catch (const Felidae::IrError &) { misplacedRecordRejected = true; }
  assert(misplacedRecordRejected);

  const auto checkRollback = [](Felidae::VmFactStore &store) {
    const auto type = store.internSymbol({1001});
    const auto field = store.internSymbol({1002});
    store.registerType(type, {}, {{field}});
    auto fact = std::make_shared<Felidae::VmFact>();
    fact->type = type;
    fact->fields = {{field, 1.0}};
    const auto original = store.retain(fact);
    assert(store.snapshotByField(field).size() == 1);
    const auto updated = store.mutate(original, field, 2.0, 0);
    const auto other = store.retain(fact);
    bool staleDeleteRejected = false;
    try {
      store.erase(std::array<Felidae::VmFactPtr, 2>{other, original});
    } catch (const Felidae::IrError &) { staleDeleteRejected = true; }
    assert(staleDeleteRejected);
    assert(store.size() == 2); // Validate the whole batch before removing any row.
    assert(store.erase(std::array<Felidae::VmFactPtr, 2>{other, other}) == 1);
    // Deserialized snapshots are equivalent values, not identical pointers.
    const Felidae::VmFactPtr fresh = std::make_shared<Felidae::VmFact>(*updated);
    store.restore(fresh, original);
    const std::array<std::pair<Felidae::IrSymbolRef, Felidae::VmValue>, 1>
        oldPredicate{{{field, 1.0}}}, newPredicate{{{field, 2.0}}};
    assert(store.snapshotMatching(type, oldPredicate).size() == 1);
    assert(store.snapshotMatching(type, newPredicate).empty());
    assert(std::get<double>(store.snapshotByField(field).front()->fields.front().second) == 1.0);
    bool staleRejected = false;
    try { store.restore(updated, original); }
    catch (const Felidae::IrError &) { staleRejected = true; }
    assert(staleRejected);
    store.erase(std::array<Felidae::VmFactPtr, 1>{original});
    bool absentRejected = false;
    try { store.restore(original, original); }
    catch (const Felidae::IrError &) { absentRejected = true; }
    assert(absentRejected);
    assert(store.snapshot().empty());
    assert(store.snapshotByField(field).empty());
  };
  {
    Felidae::VmFactStore memory;
    checkRollback(memory);
  }
  const fs::path rollbackDatabase =
      fs::path(FELIDAE_TEST_OUTPUT_DIR) / "rollback-facts.rocksdb";
  fs::remove_all(rollbackDatabase, error);
  assert(!error);
  {
    Felidae::VmFactStore persistent(rollbackDatabase);
    checkRollback(persistent);
  }
  {
    Felidae::VmFactStore reopened(rollbackDatabase);
    assert(reopened.snapshot().empty());
  }

  const fs::path ownershipDatabase =
      fs::path(FELIDAE_TEST_OUTPUT_DIR) / "ownership-facts.rocksdb";
  fs::remove_all(ownershipDatabase, error);
  assert(!error);
  {
    Felidae::VmFactStore store(ownershipDatabase);
    const auto type = store.internSymbol({1101});
    store.registerType(type, {});
    auto builder = std::make_shared<Felidae::VmFact>();
    builder->type = type;
    const auto first = store.retain(builder);
    const auto second = store.retain(builder);
    (void)store.retain(builder); // In-memory ownership: excluded from both files.
    store.recordSource(first->id, "a.csv");
    store.recordSource(second->id, "b.csv");
    assert(store.snapshotBySource("a.csv").size() == 1);
    assert(store.snapshotBySource("b.csv").size() == 1);
    store.recordSource(first->id, "b.csv");
    assert(store.snapshotBySource("a.csv").empty());
    auto rows = store.snapshotBySource("b.csv");
    assert(rows.size() == 2 && rows[0]->id == first->id &&
           rows[1]->id == second->id);
    store.erase(std::array<Felidae::VmFactPtr, 1>{first});
    assert(store.snapshotBySource("b.csv").size() == 1);
    store.restoreErased(first, "b.csv");
  }
  {
    Felidae::VmFactStore reopened(ownershipDatabase);
    assert(reopened.snapshotBySource("a.csv").empty());
    const auto rows = reopened.snapshotBySource("b.csv");
    assert(rows.size() == 2 && rows[0]->id == 1 && rows[1]->id == 2);
    assert(reopened.snapshotBySource("missing.csv").empty());
  }
}
