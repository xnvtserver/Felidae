// Section-13 audit coverage: CSV <-> Fact, round trip, multi-file ownership,
// and atomic persistence, all against real files in a temporary test-output
// directory. No mocked persistence -- Form::Db::sync and Form::Csv perform
// real disk I/O throughout, exercised the same way RegisterVm.cpp's Builtin
// dispatch uses them for CSV import and automatic fact persistence.

#include "form/IrModule.h"
#include "form/RegisterVm.h"
#include "form/libs/Builtin.h"
#include "form/libs/Csv.h"
#include "form/libs/Db.h"

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>

namespace {
using namespace Felidae;

// Identity byte encoding, matching the pattern used by FelidaeIrTest.cpp's
// testTextCodec(): each source byte becomes one PieceId, so encode/decode
// round-trip exactly without depending on any trained SentencePiece model.
Form::BuiltinTextCodec testTextCodec() {
  return {[](std::span<const PieceId> pieces) {
            std::string text;
            text.reserve(pieces.size());
            for (const auto piece : pieces)
              text.push_back(static_cast<char>(piece));
            return text;
          },
          [](std::string_view text) {
            PieceSequence pieces;
            pieces.reserve(text.size());
            for (const unsigned char character : text)
              pieces.push_back(character);
            return pieces;
          }};
}

bool rejects(const std::function<void()> &action) {
  try {
    action();
  } catch (const IrError &) {
    return true;
  } catch (const std::exception &) {
    return true;
  }
  return false;
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("test cannot open " + path.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::pair<IrSymbolRef, VmValue> field(IrSymbolRef name, VmText value) {
  return std::pair<IrSymbolRef, VmValue>(name, VmValue(std::move(value)));
}

VmFactPtr makeFact(IrSymbolRef type,
                   std::vector<std::pair<IrSymbolRef, VmValue>> fields) {
  auto fact = std::make_shared<VmFact>();
  fact->type = type;
  fact->fields = std::move(fields);
  return fact;
}

} // namespace

int main() {
  const Form::BuiltinTextCodec codec = testTextCodec();

  // LIMIT is an ordinary deterministic array operation after query lowering:
  // it preserves order, clamps oversized requests, and rejects unsafe counts.
  {
    auto rows = std::make_shared<VmArray>();
    rows->values = {10.0, 20.0, 30.0};
    const std::span<const PieceSequence> noSymbols;
    const auto limit = [&](double records) {
      return std::get<VmArrayPtr>(Form::evaluateBuiltin(
          BuiltinId::ArrayLimit,
          std::array<VmValue, 2>{VmValue{rows}, VmValue{records}}, noSymbols,
          codec));
    };
    const auto firstTwo = limit(2.0);
    assert(firstTwo->values.size() == 2);
    assert(std::get<double>(firstTwo->values[0]) == 10.0);
    assert(std::get<double>(firstTwo->values[1]) == 20.0);
    assert(limit(0.0)->values.empty());
    assert(limit(1.0e100)->values.size() == rows->values.size());
    assert(rejects([&] { (void)limit(-1.0); }));
    assert(rejects([&] { (void)limit(1.5); }));
    assert(rejects(
        [&] { (void)limit(std::numeric_limits<double>::infinity()); }));
    assert(rejects(
        [&] { (void)limit(std::numeric_limits<double>::quiet_NaN()); }));
  }

  const std::filesystem::path outDir(FELIDAE_TEST_OUTPUT_DIR);
  std::error_code ignored;
  std::filesystem::remove_all(outDir, ignored);
  std::filesystem::create_directories(outDir);

  // ---------------------------------------------------------------------
  // CSV -> Fact
  // ---------------------------------------------------------------------
  {
    // RFC4180 quoting: a quote only opens quoted mode as a field's first
    // character, so an embedded comma inside quotes stays inside one field
    // and a doubled "" decodes to one literal quote (the parseLine fix).
    const std::string csv = "name,note\n"
                            "Ada,\"hello, world\"\n"
                            "Grace,\"she said \"\"hi\"\"\"\n";
    const auto rows = Form::Csv::toFacts(csv, "Person");
    assert(rows.is_array() && rows.size() == 2);
    assert(rows[0]["__type"] == "Person");
    assert(rows[0]["name"] == "Ada");
    assert(rows[0]["note"] == "hello, world");
    assert(rows[1]["name"] == "Grace");
    assert(rows[1]["note"] == "she said \"hi\"");

    // Numeric coercion, with the leading-zero guard: "42" becomes a real
    // number, but "007" stays text (a code, not the number 7).
    const auto numeric = Form::Csv::toFacts("id,code\n42,007\n", "Row");
    assert(numeric[0]["id"].is_number());
    assert(numeric[0]["id"] == 42.0);
    assert(numeric[0]["code"].is_string());
    assert(numeric[0]["code"] == "007");

    // Blank-line handling (the parseRows fix): a blank line is skipped for
    // multi-column data, but for single-column data it is the only way to
    // represent an intentional empty-string row and must be kept.
    const auto multiColumn = Form::Csv::parse("a,b\n1,2\n\n3,4\n");
    assert(multiColumn.size() == 2);
    const auto singleColumn = Form::Csv::parse("a\nx\n\ny\n");
    assert(singleColumn.size() == 3);
    assert(singleColumn[1]["a"] == "");

    // An unterminated quoted field is a real parse error, not silently
    // truncated data.
    assert(rejects([&] { (void)Form::Csv::parse("a\n\"unterminated"); }));
  }

  // ---------------------------------------------------------------------
  // Fact search is deliberately separate from equality/predicate `where`:
  // it provides discovery over text patterns, hierarchy-valued properties,
  // and graded numeric evidence while preserving stable fact order.
  // ---------------------------------------------------------------------
  {
    FelidaeKnowledgeRuntime runtime(nullptr, 1024, 256, nullptr, nullptr,
                                    codec.decode, codec.encode);
    const std::vector<std::string> names{
        "Catalog",   "title",       "confidence", "category",
        "mode",      "query",       "case",       "direction",
        "includeSelf", "minimum",   "maximum",    "tolerance",
        "Publication", "Book",      "Magazine"};
    IrModule module;
    for (const auto &name : names)
      module.symbolTable.push_back(codec.encode(name));
    runtime.installIrModule(module);
    const auto symbol = [&](std::size_t oneBased) {
      return runtime.resolveSymbol(static_cast<IrSymbolRef>(oneBased));
    };
    runtime.registerFactType(symbol(13), {});
    runtime.registerFactType(symbol(14), {symbol(13)});
    runtime.registerFactType(symbol(15), {symbol(13)});
    runtime.registerFactType(symbol(1), {});
    const auto catalog = [&](std::string_view title, double confidence,
                             IrSymbolRef category) {
      auto fact = std::make_shared<VmFact>();
      fact->type = symbol(1);
      fact->fields = {
          {symbol(2), VmText{codec.encode(title)}},
          {symbol(3), confidence},
          {symbol(4), VmSymbol{category}},
      };
      return runtime.retainFact(fact);
    };
    const std::array<VmFactPtr, 3> facts{
        catalog("Alpha Guide", 0.82, symbol(14)),
        catalog("beta guide", 0.74, symbol(15)),
        catalog("Reference", 0.40, symbol(13))};
    const auto options = [&](std::initializer_list<
                                 std::pair<IrSymbolRef, VmValue>> entries) {
      auto result = std::make_shared<VmMap>();
      result->entries.assign(entries.begin(), entries.end());
      return result;
    };

    const auto likeRows = std::get<VmArrayPtr>(runtime.searchFacts(
        facts, codec.encode("title"),
        options({{symbol(5), VmText{codec.encode("like")}},
                 {symbol(6), VmText{codec.encode("%guide")}},
                 {symbol(7), VmText{codec.encode("insensitive")}}})));
    assert(likeRows && likeRows->values.size() == 2);

    const auto hierarchyRows = std::get<VmArrayPtr>(runtime.searchFacts(
        facts, codec.encode("category"),
        options({{symbol(5), VmText{codec.encode("hierarchy")}},
                 {symbol(6), VmSymbol{symbol(13)}},
                 {symbol(8), VmText{codec.encode("descendants")}},
                 {symbol(9), 0.0}})));
    assert(hierarchyRows && hierarchyRows->values.size() == 2);

    const auto degreeRows = std::get<VmArrayPtr>(runtime.searchFacts(
        facts, codec.encode("confidence"),
        options({{symbol(5), VmText{codec.encode("degree")}},
                 {symbol(10), 0.70},
                 {symbol(11), 0.90}})));
    assert(degreeRows && degreeRows->values.size() == 2);
  }

  // ---------------------------------------------------------------------
  // Fact -> CSV, round trip, and schema (column order) preservation
  // ---------------------------------------------------------------------
  {
    FelidaeKnowledgeRuntime runtime(nullptr, 1024, 256, nullptr, nullptr,
                                    codec.decode, codec.encode);
    IrModule module;
    module.symbolTable = {codec.encode("School")};
    runtime.installIrModule(module);
    runtime.registerFactType(/*type=*/1, /*parents=*/{});

    const std::string csv = "name,district,students\n"
                            "North,central,420\n"
                            "West,west,280\n"
                            "Lake,central,350\n";
    const auto typePieces = codec.encode("School");
    const auto path = outDir / "roundtrip_schools.csv";
    const auto sourcePieces = codec.encode(path.string());
    const auto imported = runtime.importCsvFacts(
        codec.encode(csv), typePieces, sourcePieces);
    const auto importedArray = std::get<VmArrayPtr>(imported);
    assert(importedArray && importedArray->values.size() == 3);

    const auto saved = runtime.syncDatabase(sourcePieces);
    assert(saved == 1.0);
    assert(std::filesystem::exists(path));

    const auto writtenText = readFile(path);
    // Bare parse() never coerces field types (only toFacts() does), so it is
    // the right tool for a type-agnostic check of column order alone.
    const auto writtenParsed = Form::Csv::parse(writtenText);
    assert(writtenParsed.size() == 3);
    // Schema preservation: the header column order on disk must match the
    // source CSV's column order exactly, not an arbitrary field order.
    std::vector<std::string> writtenColumns;
    for (const auto &[key, _] : writtenParsed.front().items())
      writtenColumns.push_back(key);
    assert((writtenColumns == std::vector<std::string>{"name", "district",
                                                        "students"}));
    // Round trip: values, including numeric coercion, survive
    // CSV -> Fact -> CSV -> Fact unchanged.
    const auto writtenRows = Form::Csv::toFacts(writtenText, "School");
    assert(writtenRows[0]["name"] == "North");
    assert(writtenRows[0]["district"] == "central");
    assert(writtenRows[0]["students"].is_number());
    assert(writtenRows[0]["students"] == 420.0);
    assert(writtenRows[1]["name"] == "West");
    assert(writtenRows[2]["district"] == "central");
  }

  // ---------------------------------------------------------------------
  // Multi-file ownership: two fact types imported from two different
  // source files must sync back to exactly their own file, never the
  // other type's rows and never the whole store.
  // ---------------------------------------------------------------------
  {
    FelidaeKnowledgeRuntime runtime(nullptr, 1024, 256, nullptr, nullptr,
                                    codec.decode, codec.encode);
    IrModule module;
    module.symbolTable = {codec.encode("School"), codec.encode("Teacher")};
    runtime.installIrModule(module);
    runtime.registerFactType(/*type=*/1, /*parents=*/{});
    runtime.registerFactType(/*type=*/2, /*parents=*/{});

    const auto schoolsPath = outDir / "ownership_schools.csv";
    const auto teachersPath = outDir / "ownership_teachers.csv";
    const auto schoolsSource = codec.encode(schoolsPath.string());
    const auto teachersSource = codec.encode(teachersPath.string());

    const auto schoolsImported = runtime.importCsvFacts(
        codec.encode("name,district\nNorth,central\nWest,west\n"), codec.encode("School"),
        schoolsSource);
    const auto teachersImported = runtime.importCsvFacts(
        codec.encode("name,subject\nAda,math\nBo,science\nCy,art\n"),
        codec.encode("Teacher"), teachersSource);
    assert(std::get<VmArrayPtr>(schoolsImported)->values.size() == 2);
    assert(std::get<VmArrayPtr>(teachersImported)->values.size() == 3);

    runtime.syncDatabase(schoolsSource);
    runtime.syncDatabase(teachersSource);

    const auto schoolsRows = Form::Csv::parse(readFile(schoolsPath));
    const auto teachersRows = Form::Csv::parse(readFile(teachersPath));
    assert(schoolsRows.size() == 2);
    assert(teachersRows.size() == 3);
    for (const auto &row : schoolsRows) {
      assert(row.contains("district"));
      assert(!row.contains("subject"));
    }
    for (const auto &row : teachersRows) {
      assert(row.contains("subject"));
      assert(!row.contains("district"));
    }
  }

  // ---------------------------------------------------------------------
  // Automatic DML persistence: update, inferred-source insert, and delete
  // each commit the owning CSV without a public db.sync() call.
  // ---------------------------------------------------------------------
  {
    FelidaeKnowledgeRuntime runtime(nullptr, 1024, 256, nullptr, nullptr,
                                    codec.decode, codec.encode);
    IrModule module;
    module.symbolTable = {codec.encode("School")};
    runtime.installIrModule(module);
    runtime.registerFactType(/*type=*/1, /*parents=*/{});

    const auto path = outDir / "automatic_dml.csv";
    const auto source = codec.encode(path.string());
    const auto type = codec.encode("School");
    const auto imported = std::get<VmArrayPtr>(runtime.importCsvFacts(
        codec.encode("name,students\nNorth,420\nWest,280\n"), type,
        source));
    assert(imported && imported->values.size() == 2);
    const auto north = std::get<VmFactPtr>(imported->values[0]);
    const auto west = std::get<VmFactPtr>(imported->values[1]);
    assert(north && west && north->fields.size() == 2);

    auto update = std::make_shared<VmMap>();
    update->entries.emplace_back(north->fields[1].first, 425.0);
    const std::array<VmFactPtr, 1> updateTargets{north};
    (void)runtime.updateFacts(updateTargets, update);
    auto rows = Form::Csv::toFacts(readFile(path), "School");
    assert(rows.size() == 2 && rows[0]["students"] == 425.0);

    auto insertedValues = std::make_shared<VmMap>();
    insertedValues->entries.emplace_back(
        north->fields[0].first, VmText{codec.encode("Lake")});
    insertedValues->entries.emplace_back(north->fields[1].first, 350.0);
    const auto inserted = runtime.insertFact(type, insertedValues, std::nullopt);
    assert(inserted && runtime.factStore()->sourceOf(inserted->id) ==
                           std::optional<std::string>{path.string()});
    rows = Form::Csv::toFacts(readFile(path), "School");
    assert(rows.size() == 3 && rows[2]["name"] == "Lake");

    const std::array<VmFactPtr, 1> deleteTargets{west};
    assert(runtime.deleteFacts(deleteTargets) == 1.0);
    rows = Form::Csv::toFacts(readFile(path), "School");
    assert(rows.size() == 2);
    assert(rows[0]["name"] == "North");
    assert(rows[1]["name"] == "Lake");

    const auto remaining = runtime.factStore()->snapshot(/*type=*/1);
    assert(runtime.deleteFacts(remaining) == 2.0);
    assert(readFile(path) == "name,students\n");
    assert(Form::Csv::toFacts(readFile(path), "School").empty());
  }

  // ---------------------------------------------------------------------
  // Native .fx fact databases use the same ownership and transaction path
  // as CSV. An explicit source establishes ownership for the first insert;
  // later inserts infer the sole source for that fact type.
  // ---------------------------------------------------------------------
  {
    FelidaeKnowledgeRuntime runtime(nullptr, 1024, 256, nullptr, nullptr,
                                    codec.decode, codec.encode);
    IrModule module;
    module.symbolTable = {codec.encode("School"), codec.encode("name"),
                          codec.encode("students")};
    runtime.installIrModule(module);
    runtime.registerFactType(/*type=*/1, /*parents=*/{});

    const auto path = outDir / "automatic_dml.fx";
    const auto type = codec.encode("School");
    auto northValues = std::make_shared<VmMap>();
    northValues->entries.emplace_back(2, VmText{codec.encode("North")});
    northValues->entries.emplace_back(3, 420.0);
    const auto source = codec.encode(path.string());
    const auto north = runtime.insertFact(type, northValues, source);
    assert(north && readFile(path).find("students: 420.0") !=
                        std::string::npos);

    auto westValues = std::make_shared<VmMap>();
    westValues->entries.emplace_back(2, VmText{codec.encode("West")});
    westValues->entries.emplace_back(3, 280.0);
    const auto west = runtime.insertFact(type, westValues, std::nullopt);
    assert(west && runtime.factStore()->sourceOf(west->id) ==
                       std::optional<std::string>{path.string()});

    auto update = std::make_shared<VmMap>();
    update->entries.emplace_back(3, 425.0);
    (void)runtime.updateFacts(std::array<VmFactPtr, 1>{north}, update);
    auto sourceText = readFile(path);
    assert(sourceText.find("students: 425.0") != std::string::npos);
    assert(sourceText.find("students: 280.0") != std::string::npos);

    assert(runtime.deleteFacts(std::array<VmFactPtr, 1>{west}) == 1.0);
    sourceText = readFile(path);
    assert(sourceText.find("West") == std::string::npos);
    assert(sourceText.find("North") != std::string::npos);
  }

  // ---------------------------------------------------------------------
  // Runtime transaction rollback. A source with an unsupported extension
  // is accepted as import ownership metadata, but persistence must fail at
  // the database boundary. Insert/update/delete must then leave the retained
  // facts and mutation journal exactly as they were before the operation.
  // ---------------------------------------------------------------------
  {
    FelidaeKnowledgeRuntime runtime(nullptr, 1024, 256, nullptr, nullptr,
                                    codec.decode, codec.encode);
    IrModule module;
    module.symbolTable = {codec.encode("School"), codec.encode("name"),
                          codec.encode("students")};
    runtime.installIrModule(module);
    runtime.registerFactType(/*type=*/1, /*parents=*/{});

    const auto type = codec.encode("School");
    const auto invalidSource = codec.encode((outDir / "invalid.txt").string());
    const auto imported = std::get<VmArrayPtr>(runtime.importCsvFacts(
        codec.encode("name,students\nNorth,420\n"), type, invalidSource));
    const auto north = std::get<VmFactPtr>(imported->values.front());
    const auto journalBefore = runtime.factStore()->mutations();

    auto update = std::make_shared<VmMap>();
    update->entries.emplace_back(3, 425.0);
    assert(rejects([&] {
      (void)runtime.updateFacts(std::array<VmFactPtr, 1>{north}, update);
    }));
    auto retained = runtime.factStore()->snapshot(/*type=*/1);
    assert(retained.size() == 1);
    assert(std::get<double>(retained.front()->fields[1].second) == 420.0);
    assert(runtime.factStore()->mutations().size() == journalBefore.size());

    assert(rejects([&] {
      (void)runtime.deleteFacts(std::array<VmFactPtr, 1>{retained.front()});
    }));
    retained = runtime.factStore()->snapshot(/*type=*/1);
    assert(retained.size() == 1 && retained.front()->id == north->id);
    assert(runtime.factStore()->sourceOf(north->id).has_value());
    assert(runtime.factStore()->mutations().size() == journalBefore.size());
  }

  {
    FelidaeKnowledgeRuntime runtime(nullptr, 1024, 256, nullptr, nullptr,
                                    codec.decode, codec.encode);
    IrModule module;
    module.symbolTable = {codec.encode("School"), codec.encode("name")};
    runtime.installIrModule(module);
    runtime.registerFactType(/*type=*/1, /*parents=*/{});
    auto values = std::make_shared<VmMap>();
    values->entries.emplace_back(2, VmText{codec.encode("North")});
    const auto invalidSource = codec.encode((outDir / "insert.txt").string());
    assert(rejects(
        [&] { (void)runtime.insertFact(codec.encode("School"), values,
                                      invalidSource); }));
    assert(runtime.factStore()->snapshot(/*type=*/1).empty());
    assert(runtime.factStore()->mutations().empty());
  }

  // ---------------------------------------------------------------------
  // Multi-source transactions are deterministic and source-local. A valid
  // earlier source remains committed if a later source fails, while facts
  // owned by the failing source are restored to their previous snapshots.
  // ---------------------------------------------------------------------
  {
    FelidaeKnowledgeRuntime runtime(nullptr, 1024, 256, nullptr, nullptr,
                                    codec.decode, codec.encode);
    IrModule module;
    module.symbolTable = {codec.encode("School"), codec.encode("name"),
                          codec.encode("students")};
    runtime.installIrModule(module);
    runtime.registerFactType(/*type=*/1, /*parents=*/{});

    const auto committedPath = outDir / "a-committed.csv";
    const auto failedPath = outDir / "z-failed.txt";
    const auto type = codec.encode("School");
    const auto committedRows = std::get<VmArrayPtr>(runtime.importCsvFacts(
        codec.encode("name,students\nNorth,420\n"), type,
        codec.encode(committedPath.string())));
    const auto failedRows = std::get<VmArrayPtr>(runtime.importCsvFacts(
        codec.encode("name,students\nWest,280\n"), type,
        codec.encode(failedPath.string())));
    const std::array<VmFactPtr, 2> targets{
        std::get<VmFactPtr>(committedRows->values.front()),
        std::get<VmFactPtr>(failedRows->values.front())};
    auto update = std::make_shared<VmMap>();
    update->entries.emplace_back(3, 500.0);
    std::string failure;
    try {
      (void)runtime.updateFacts(targets, update);
    } catch (const IrError &error) {
      failure = error.what();
    }
    assert(!failure.empty());
    assert(failure.find(failedPath.string()) != std::string::npos);
    assert(failure.find(committedPath.string()) != std::string::npos);
    const auto committed = Form::Csv::toFacts(readFile(committedPath),
                                               "School");
    assert(committed.size() == 1 && committed[0]["students"] == 500.0);
    const auto retained = runtime.factStore()->snapshot(/*type=*/1);
    assert(retained.size() == 2);
    assert(std::get<double>(retained[0]->fields[1].second) == 500.0);
    assert(std::get<double>(retained[1]->fields[1].second) == 280.0);
  }

  // ---------------------------------------------------------------------
  // Atomic sync: a failed sync (mixed/mismatched fact schema) must never
  // touch the file already on disk, and must not leave a stray .tmp file
  // behind. Calls Form::Db::sync directly with a deliberately invalid
  // facts span to force the failure Form::Db::sync's own schema check
  // raises, bypassing the ownership tracking that would normally prevent
  // this from ever being assembled through normal owned-fact persistence.
  // ---------------------------------------------------------------------
  {
    const auto path = outDir / "atomic.csv";
    std::vector<std::pair<IrSymbolRef, VmValue>> adaFields;
    adaFields.push_back(field(2, VmText{codec.encode("Ada")}));
    std::vector<std::pair<IrSymbolRef, VmValue>> boFields;
    boFields.push_back(field(2, VmText{codec.encode("Bo")}));
    std::vector<VmFactPtr> validFacts;
    validFacts.push_back(makeFact(1, std::move(adaFields)));
    validFacts.push_back(makeFact(1, std::move(boFields)));
    std::vector<PieceSequence> symbolTable;
    symbolTable.push_back(codec.encode("Person"));
    symbolTable.push_back(codec.encode("name"));
    Form::Db::sync(path, validFacts, symbolTable, codec.decode);
    assert(std::filesystem::exists(path));
    const auto before = readFile(path);
    assert(!before.empty());

    // A second fact with a different field count than the first violates
    // "one stable CSV field order" -- Form::Db::sync must reject this
    // before ever writing to the target path.
    std::vector<std::pair<IrSymbolRef, VmValue>> cyFields;
    cyFields.push_back(field(2, VmText{codec.encode("Cy")}));
    std::vector<std::pair<IrSymbolRef, VmValue>> deeFields;
    deeFields.push_back(field(2, VmText{codec.encode("Dee")}));
    deeFields.push_back(field(3, VmText{codec.encode("extra")}));
    std::vector<VmFactPtr> mismatchedFacts;
    mismatchedFacts.push_back(makeFact(1, std::move(cyFields)));
    mismatchedFacts.push_back(makeFact(1, std::move(deeFields)));
    std::vector<PieceSequence> mismatchedSymbolTable;
    mismatchedSymbolTable.push_back(codec.encode("Person"));
    mismatchedSymbolTable.push_back(codec.encode("name"));
    mismatchedSymbolTable.push_back(codec.encode("extra"));
    assert(rejects([&] {
      Form::Db::sync(path, mismatchedFacts, mismatchedSymbolTable, codec.decode);
    }));

    const auto after = readFile(path);
    assert(before == after);
    assert(!std::filesystem::exists(
        std::filesystem::path(path.string() + ".tmp")));
  }

  std::filesystem::remove_all(outDir, ignored);
  return 0;
}
