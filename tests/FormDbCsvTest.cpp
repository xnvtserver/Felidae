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

#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
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
