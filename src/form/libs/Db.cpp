#include "Db.h"

#include "Builtin.h"
#include "Csv.h"
#include "Json.h"
#include "form/IrModule.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Felidae::Form::Db {
namespace {

std::mutex databaseMutex;

class DatabaseFileLock {
public:
  explicit DatabaseFileLock(const std::filesystem::path &database) {
    auto lockPath = database;
    lockPath += ".lock";
    if (lockPath.has_parent_path())
      std::filesystem::create_directories(lockPath.parent_path());
#ifdef _WIN32
    handle_ = CreateFileW(lockPath.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                          0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                          nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
      throw IrError("fact database is busy: " + database.string());
#else
    descriptor_ = ::open(lockPath.c_str(), O_CREAT | O_RDWR, 0666);
    if (descriptor_ < 0 || flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
      if (descriptor_ >= 0)
        ::close(descriptor_);
      descriptor_ = -1;
      throw IrError("fact database is busy: " + database.string());
    }
#endif
  }

  ~DatabaseFileLock() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(handle_);
#else
    if (descriptor_ >= 0) {
      flock(descriptor_, LOCK_UN);
      ::close(descriptor_);
    }
#endif
  }

  DatabaseFileLock(const DatabaseFileLock &) = delete;
  DatabaseFileLock &operator=(const DatabaseFileLock &) = delete;

private:
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int descriptor_ = -1;
#endif
};

std::string numberText(double value) {
  if (!std::isfinite(value))
    throw IrError("fact database cannot serialize a non-finite number");
  std::ostringstream output;
  output << std::setprecision(std::numeric_limits<double>::max_digits10)
         << value;
  auto text = output.str();
  if (text.find_first_of(".eE") == std::string::npos)
    text += ".0";
  return text;
}

std::string indentation(std::size_t width) { return std::string(width, ' '); }

std::string sourceName(std::string value) {
  if (value.empty() ||
      !(std::isalpha(static_cast<unsigned char>(value.front())) ||
        value.front() == '_') ||
      !std::all_of(value.begin() + 1, value.end(), [](unsigned char byte) {
        return std::isalnum(byte) || byte == '_';
      })) {
    throw IrError("fact database symbol cannot be written as a source name");
  }
  return value;
}

std::string valueText(const VmValue &value,
                      std::span<const PieceSequence> symbolTable,
                      const VmTextDecoder &decodeText, std::size_t indent,
                      std::size_t depth);

template <typename Entries, typename KeyText>
std::string mapText(const Entries &entries, KeyText keyText,
                    std::span<const PieceSequence> symbolTable,
                    const VmTextDecoder &decodeText, std::size_t indent,
                    std::size_t depth) {
  if (entries.empty())
    return "{}";
  std::ostringstream output;
  output << "{\n";
  for (std::size_t index = 0; index < entries.size(); ++index) {
    output << indentation(indent + 4) << keyText(entries[index].first) << ": "
           << valueText(entries[index].second, symbolTable, decodeText,
                        indent + 4, depth + 1);
    if (index + 1 < entries.size())
      output << ',';
    output << '\n';
  }
  output << indentation(indent) << '}';
  return output.str();
}

std::string factText(const VmFact &fact,
                     std::span<const PieceSequence> symbolTable,
                     const VmTextDecoder &decodeText, std::size_t indent,
                     std::size_t depth) {
  const auto type =
      sourceName(decodeText(irSymbolPieces(symbolTable, fact.type)));
  std::ostringstream output;
  output << type << '(';
  if (!fact.fields.empty()) {
    output << '\n';
    for (std::size_t index = 0; index < fact.fields.size(); ++index) {
      const auto &[field, value] = fact.fields[index];
      output << indentation(indent + 4)
             << sourceName(decodeText(irSymbolPieces(symbolTable, field)))
             << ": "
             << valueText(value, symbolTable, decodeText, indent + 4,
                          depth + 1);
      if (index + 1 < fact.fields.size())
        output << ',';
      output << '\n';
    }
    output << indentation(indent);
  }
  output << ')';
  return output.str();
}

std::string valueText(const VmValue &value,
                      std::span<const PieceSequence> symbolTable,
                      const VmTextDecoder &decodeText, std::size_t indent,
                      std::size_t depth) {
  constexpr std::size_t kMaximumPersistenceDepth = 64;
  if (depth > kMaximumPersistenceDepth)
    throw IrError("fact database value nesting is too deep");
  if (std::holds_alternative<VmNil>(value))
    return "nil";
  if (const auto number = std::get_if<double>(&value))
    return numberText(*number);
  if (const auto degree = std::get_if<VmDegree>(&value))
    return numberText(degree->value);
  if (const auto text = std::get_if<VmText>(&value))
    return Json::toText(Json::Value(decodeText(text->pieces)));
  if (const auto symbol = std::get_if<VmSymbol>(&value))
    return decodeText(irSymbolPieces(symbolTable, symbol->value));
  if (const auto array = std::get_if<VmArrayPtr>(&value); array && *array) {
    if ((*array)->values.empty())
      return "[]";
    std::ostringstream output;
    output << "[\n";
    for (std::size_t index = 0; index < (*array)->values.size(); ++index) {
      output << indentation(indent + 4)
             << valueText((*array)->values[index], symbolTable, decodeText,
                          indent + 4, depth + 1);
      if (index + 1 < (*array)->values.size())
        output << ',';
      output << '\n';
    }
    output << indentation(indent) << ']';
    return output.str();
  }
  if (const auto map = std::get_if<VmMapPtr>(&value); map && *map) {
    return mapText(
        (*map)->entries,
        [&](IrSymbolRef key) {
          return sourceName(decodeText(irSymbolPieces(symbolTable, key)));
        },
        symbolTable, decodeText, indent, depth);
  }
  if (const auto map = std::get_if<VmTextMapPtr>(&value); map && *map) {
    return mapText((*map)->entries,
                   [&](const PieceSequence &key) {
                     return sourceName(decodeText(key));
                   },
                   symbolTable, decodeText, indent, depth);
  }
  if (const auto fact = std::get_if<VmFactPtr>(&value); fact && *fact)
    return factText(**fact, symbolTable, decodeText, indent, depth);
  if (std::holds_alternative<VmTensorPtr>(value))
    throw IrError("fact database cannot serialize a tensor value");
  throw IrError("fact database contains an invalid VM value");
}

void atomicWrite(const std::filesystem::path &path, std::string_view content) {
  if (path.has_parent_path())
    std::filesystem::create_directories(path.parent_path());
  auto temporary = path;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
      throw IrError("cannot write temporary fact database: " +
                    temporary.string());
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output)
      throw IrError("cannot complete temporary fact database: " +
                    temporary.string());
  }
#ifdef _WIN32
  if (!MoveFileExW(temporary.wstring().c_str(), path.wstring().c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(temporary);
    throw IrError("cannot replace fact database: " + path.string());
  }
#else
  if (std::rename(temporary.string().c_str(), path.string().c_str()) != 0) {
    std::filesystem::remove(temporary);
    throw IrError("cannot replace fact database: " + path.string());
  }
#endif
}

std::string csvText(std::span<const VmFactPtr> facts,
                    std::span<const PieceSequence> symbolTable,
                    const VmTextDecoder &decodeText) {
  if (facts.empty())
    return {};
  if (!facts.front())
    throw IrError("CSV database contains an invalid retained fact");
  const auto type = facts.front()->type;
  std::vector<IrSymbolRef> fields;
  fields.reserve(facts.front()->fields.size());
  for (const auto &[field, _] : facts.front()->fields)
    fields.push_back(field);

  const BuiltinTextCodec codec{
      decodeText,
      [](std::string_view) -> PieceSequence {
        throw IrError("CSV persistence unexpectedly requested text encoding");
      }};
  auto rows = Json::Value::array();
  for (const auto &fact : facts) {
    if (!fact || fact->type != type || fact->fields.size() != fields.size())
      throw IrError("db.sync cannot mix CSV fact types or schemas");
    for (std::size_t index = 0; index < fields.size(); ++index) {
      if (fact->fields[index].first != fields[index])
        throw IrError("db.sync requires one stable CSV field order");
    }
    rows.push_back(vmValueToJson(VmValue{fact}, symbolTable, codec));
  }
  return Csv::toText(rows);
}

} // namespace

void sync(const std::filesystem::path &path,
          std::span<const VmFactPtr> facts,
          std::span<const PieceSequence> symbolTable,
          const VmTextDecoder &decodeText) {
  if (path.empty() || (path.extension() != ".fx" &&
                       path.extension() != ".csv"))
    throw IrError("db.sync requires a non-empty .fx or .csv database path");
  if (!decodeText)
    throw IrError("db.sync requires the VM SentencePiece decoder");
  std::string content;
  if (path.extension() == ".csv") {
    content = csvText(facts, symbolTable, decodeText);
  } else {
    std::ostringstream source;
    source << "# Felidae fact database\n";
    for (const auto &fact : facts) {
      if (!fact)
        throw IrError("fact database contains an invalid retained fact");
      source << '\n' << factText(*fact, symbolTable, decodeText, 0, 0) << '\n';
    }
    content = source.str();
  }
  std::lock_guard lock(databaseMutex);
  DatabaseFileLock fileLock(path);
  atomicWrite(path, content);
}

} // namespace Felidae::Form::Db
