#include "CompilerFrontend.h"

#include "BuiltinRegistry.h"
#include "IntegerParser.h"
#include "IntegerTokenList.h"
#include "IrCodeGenerator.h"
#include "Operator.h"
#include "SentencePieceModel.h"
#include "Symbol.h"
#include "form/RegisterVm.h"

#include <sentencepiece_processor.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace Felidae {
namespace {
constexpr std::uintmax_t kStreamingReadThresholdBytes =
    10ull * 1024ull * 1024ull;
constexpr std::size_t kReadChunkBytes = 1024ull * 1024ull;

std::string normalizeSource(std::string text) {
  // A UTF-8 BOM is transport metadata, not Felidae syntax. Retaining it
  // creates an unexpected first SentencePiece token before valid syntax.
  constexpr char kUtf8Bom[] = {'\xEF', '\xBB', '\xBF'};
  if (text.size() >= std::size(kUtf8Bom) &&
      std::equal(std::begin(kUtf8Bom), std::end(kUtf8Bom), text.begin())) {
    text.erase(0, std::size(kUtf8Bom));
  }
  return text;
}
} // namespace

std::string readSourceFile(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("Cannot open file: " + path.string());
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (!ec && size <= kStreamingReadThresholdBytes) {
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!text.empty()) {
      in.read(text.data(), static_cast<std::streamsize>(text.size()));
      if (!in && !in.eof())
        throw std::runtime_error("Cannot read file: " + path.string());
    }
    return normalizeSource(std::move(text));
  }
  std::string text;
  if (!ec)
    text.reserve(static_cast<std::size_t>(
        std::min<std::uintmax_t>(size, kStreamingReadThresholdBytes)));
  std::vector<char> buffer(kReadChunkBytes);
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto read = in.gcount();
    if (read > 0)
      text.append(buffer.data(), static_cast<std::size_t>(read));
  }
  if (!in.eof())
    throw std::runtime_error("Cannot read file: " + path.string());
  return normalizeSource(std::move(text));
}

std::filesystem::path resolveProgramEntryPath(const fs::path &path) {
  fs::path normalized = fs::absolute(path).lexically_normal();
  std::error_code ec;
  if (!fs::is_directory(normalized, ec))
    return normalized;
  const fs::path mainFile = normalized / "main.fx";
  if (fs::exists(mainFile, ec) && fs::is_regular_file(mainFile, ec))
    return mainFile.lexically_normal();
  throw std::runtime_error("Project directory does not contain main.fx: " +
                           normalized.string());
}

Program parseProgramText(std::string text, const CompilerOptions &options) {
  return parseProgramText(std::move(text), std::make_shared<OperatorRegistry>(),
                          options);
}

Program parseProgramText(std::string text,
                         std::shared_ptr<OperatorRegistry> operators,
                         const CompilerOptions &options) {
  if (!operators)
    throw std::invalid_argument("parser operator registry must not be null");
  IntegerTokenList input(felidaeSentencePieceModel(), std::move(text));
  // Mixfix declarations and uses share one parser-owned registry while each
  // physical source line is independently SentencePiece-encoded.
  IntegerParser parser(input, std::move(operators), options.mixfixModel);
  return parser.parseProgram();
}

Program parseProgramFile(const fs::path &path, const CompilerOptions &options) {
  return parseProgramText(readSourceFile(resolveProgramEntryPath(path)),
                          options);
}

namespace {

PieceSequence encodePersistenceSource(const fs::path &path) {
  std::vector<int> encoded;
  const auto spelling = path.string();
  const auto status = felidaeSentencePieceModel().Encode(spelling, &encoded);
  if (!status.ok() || encoded.empty()) {
    throw IntegerParserError("fact database path cannot be encoded: " +
                             spelling);
  }
  PieceSequence pieces;
  pieces.reserve(encoded.size());
  for (const int piece : encoded) {
    if (piece < 0)
      throw IntegerParserError(
          "SentencePiece emitted an invalid fact database path piece");
    pieces.push_back(static_cast<PieceId>(piece));
  }
  return pieces;
}

fs::path resolveFactDatabasePath(const fs::path &importer,
                                 std::string_view imported) {
  fs::path candidate{std::string(imported)};
  if (candidate.is_relative())
    candidate = importer.parent_path() / candidate;
  candidate = fs::absolute(candidate).lexically_normal();
  std::error_code error;
  if (!fs::is_regular_file(candidate, error)) {
    throw IntegerParserError("fact database import is not a file: " +
                             candidate.string());
  }
  const auto canonical = fs::weakly_canonical(candidate, error);
  return error ? candidate : canonical;
}

class FactDatabaseLinker {
public:
  explicit FactDatabaseLinker(const CompilerOptions &options)
      : options_(options) {}

  void link(const fs::path &path) {
    const auto key = path.string();
    if (linked_.contains(key))
      return;
    if (!visiting_.insert(key).second)
      throw IntegerParserError("cyclic fact database import: " + key);

    Program database;
    try {
      database = parseProgramText(readSourceFile(path), options_);
    } catch (const std::exception &error) {
      visiting_.erase(key);
      throw IntegerParserError("cannot parse fact database '" + key +
                               "': " + error.what());
    }
    for (const auto &import : database.imports) {
      if (!import)
        throw IntegerParserError("invalid import in fact database: " + key);
      for (const auto &name : import->paths) {
        if (isBuiltinModuleName(name))
          continue;
        if (fs::path(name).extension() != ".fx") {
          throw IntegerParserError("unsupported fact database import '" +
                                   name + "' in " + key);
        }
        link(resolveFactDatabasePath(path, name));
      }
    }
    if (!database.globals.empty()) {
      throw IntegerParserError("fact database must not declare globals: " +
                               key);
    }
    const auto owner = std::make_shared<const PieceSequence>(
        encodePersistenceSource(path));
    for (auto &clause : database.clauses) {
      if (!clause || !clause->isFact() || !clause->annotations.empty()) {
        throw IntegerParserError(
            "fact database may contain only unannotated fact rows: " + key);
      }
      clause->persistenceSource = owner;
      facts_.push_back(std::move(clause));
    }
    visiting_.erase(key);
    linked_.insert(key);
  }

  std::vector<std::shared_ptr<ClauseStmt>> takeFacts() {
    return std::move(facts_);
  }

private:
  const CompilerOptions &options_;
  std::unordered_set<std::string> visiting_;
  std::unordered_set<std::string> linked_;
  std::vector<std::shared_ptr<ClauseStmt>> facts_;
};

Program linkFactDatabases(Program program, const fs::path &entry,
                          const CompilerOptions &options) {
  FactDatabaseLinker linker(options);
  std::vector<std::shared_ptr<ImportStmt>> remainingImports;
  for (const auto &import : program.imports) {
    if (!import)
      throw IntegerParserError("invalid source import");
    std::vector<std::string> remainingPaths;
    for (const auto &name : import->paths) {
      if (fs::path(name).extension() == ".fx")
        linker.link(resolveFactDatabasePath(entry, name));
      else
        remainingPaths.push_back(name);
    }
    if (!remainingPaths.empty())
      remainingImports.push_back(
          std::make_shared<ImportStmt>(std::move(remainingPaths)));
  }

  Program linked;
  for (auto &import : remainingImports)
    linked.addStatement(std::move(import));
  for (auto &fact : linker.takeFacts())
    linked.addStatement(std::move(fact));
  for (auto &global : program.globals)
    linked.addStatement(std::move(global));
  std::unordered_set<const ClauseStmt *> classMethods;
  for (const auto &declaration : program.classes)
    for (const auto &method : declaration->methods)
      classMethods.insert(method.get());
  for (auto &declaration : program.classes)
    linked.addStatement(std::move(declaration));
  for (auto &clause : program.clauses)
    if (!classMethods.contains(clause.get()))
      linked.addStatement(std::move(clause));
  return linked;
}

} // namespace

static IrModule compileProgramToIr(Program program) {
  auto module = IrCodeGenerator{}.compile(std::move(program));
  std::vector<IrSymbolRef> symbols;
  const auto collectProgram = [&](const FelidaeIr &program) {
    symbols.insert(symbols.end(), program.symbols.begin(),
                   program.symbols.end());
  };
  collectProgram(module.ir);
  symbols.push_back(module.entryProcedure);
  for (const auto &[symbol, procedure] : module.procedures) {
    symbols.push_back(symbol);
    collectProgram(procedure.ir);
    symbols.insert(symbols.end(), procedure.positionalParameters.begin(),
                   procedure.positionalParameters.end());
    symbols.insert(symbols.end(), procedure.namedParameters.begin(),
                   procedure.namedParameters.end());
  }
  for (const auto &type : module.factTypes) {
    symbols.push_back(type.symbol);
    symbols.insert(symbols.end(), type.parents.begin(), type.parents.end());
    for (const auto &index : type.indexes)
      symbols.insert(symbols.end(), index.begin(), index.end());
  }
  std::sort(symbols.begin(), symbols.end());
  symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
  std::unordered_map<IrSymbolRef, IrSymbolRef> indexes;
  indexes.reserve(symbols.size());
  module.symbolTable.reserve(symbols.size());
  const auto &model = felidaeSentencePieceModel();
  for (const auto symbol : symbols) {
    const auto spelling = symbolNameForId(symbol);
    if (symbol == 0 || spelling.empty())
      throw IntegerParserError("IR symbol has no canonical lexical spelling");
    PieceSequence pieces = symbolPiecesForId(symbol);
    if (pieces.empty()) {
      // Compiler-synthesized composite names have no source token span.
      // Encode those once at this lexical boundary; parsed names reuse
      // the exact PieceIds retained by IntegerParser.
      std::vector<int> encoded;
      const auto status = model.Encode(spelling, &encoded);
      if (!status.ok() || encoded.empty())
        throw IntegerParserError(
            "IR symbol cannot be encoded by the fixed SentencePiece model");
      pieces.reserve(encoded.size());
      for (const auto piece : encoded) {
        if (piece < 0)
          throw IntegerParserError(
              "SentencePiece emitted an invalid symbol piece");
        pieces.push_back(static_cast<PieceId>(piece));
      }
    }
    const auto index = static_cast<IrSymbolRef>(module.symbolTable.size() + 1);
    indexes.emplace(symbol, index);
    module.symbolTable.push_back(std::move(pieces));
  }
  const auto remap = [&](IrSymbolRef &symbol) { symbol = indexes.at(symbol); };
  const auto remapProgram = [&](FelidaeIr &program) {
    for (auto &symbol : program.symbols)
      remap(symbol);
  };
  remapProgram(module.ir);
  remap(module.entryProcedure);
  std::unordered_map<IrSymbolRef, IrProcedure> procedures;
  procedures.reserve(module.procedures.size());
  for (auto &[symbol, procedure] : module.procedures) {
    remapProgram(procedure.ir);
    for (auto &parameter : procedure.positionalParameters)
      remap(parameter);
    for (auto &parameter : procedure.namedParameters)
      remap(parameter);
    procedures.emplace(indexes.at(symbol), std::move(procedure));
  }
  module.procedures = std::move(procedures);
  for (auto &type : module.factTypes) {
    remap(type.symbol);
    for (auto &parent : type.parents)
      remap(parent);
    for (auto &index : type.indexes)
      for (auto &field : index)
        remap(field);
  }
  module.sentencePieceModelIdentity = felidaeSentencePieceModelIdentity();
  return module;
}

IrModule compileProgramTextToIr(std::string text,
                                const CompilerOptions &options) {
  return compileProgramToIr(parseProgramText(std::move(text), options));
}

IrModule compileProgramFileToIr(const fs::path &path,
                                const CompilerOptions &options) {
  const auto entry = resolveProgramEntryPath(path);
  auto program = parseProgramText(readSourceFile(entry), options);
  return compileProgramToIr(linkFactDatabases(std::move(program), entry,
                                              options));
}

std::optional<FelidaeIr> tryCompileExpressionTextToIr(const std::string &text) {
  try {
    IntegerTokenList input(felidaeSentencePieceModel(), text);
    return IntegerParser(input, std::make_shared<OperatorRegistry>())
        .compileExpressionIr();
  } catch (const IntegerParserError &) {
    return std::nullopt;
  }
}

} // namespace Felidae
