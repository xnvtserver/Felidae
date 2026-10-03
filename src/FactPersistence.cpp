#include "Interpreter.h"

#include "FelidaeRuntime.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Felidae {
namespace fs = std::filesystem;
namespace {

std::mutex persistenceMutex;
std::atomic<std::uint64_t> temporarySequence{1};

class FactFileLock {
public:
    explicit FactFileLock(const fs::path& database) {
        auto lockPath = database;
        lockPath += ".lock";
        if (lockPath.has_parent_path()) fs::create_directories(lockPath.parent_path());
#ifdef _WIN32
        handle_ = CreateFileW(lockPath.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            throw InterpreterError("fact database is busy: " + database.string());
#else
        descriptor_ = ::open(lockPath.c_str(), O_CREAT | O_RDWR, 0666);
        if (descriptor_ < 0 || flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            if (descriptor_ >= 0) ::close(descriptor_);
            descriptor_ = -1;
            throw InterpreterError("fact database is busy: " + database.string());
        }
#endif
    }

    ~FactFileLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (descriptor_ >= 0) {
            flock(descriptor_, LOCK_UN);
            ::close(descriptor_);
        }
#endif
    }

    FactFileLock(const FactFileLock&) = delete;
    FactFileLock& operator=(const FactFileLock&) = delete;

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

fs::path normalizedSource(const fs::path& source) {
    if (source.empty()) return {};
    return fs::absolute(source).lexically_normal();
}

std::string sourceName(const std::string& name) {
    if (name.empty() ||
        !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_') ||
        !std::all_of(name.begin() + 1, name.end(), [](unsigned char byte) {
            return std::isalnum(byte) || byte == '_';
        })) {
        throw InterpreterError("fact database symbol cannot be written as a source name: " + name);
    }
    return name;
}

std::string numberSource(double value) {
    if (!std::isfinite(value))
        throw InterpreterError("fact database cannot serialize a non-finite number");
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    auto text = output.str();
    if (text.find_first_of(".eE") == std::string::npos) text += ".0";
    return text;
}

std::string quotedSource(std::string_view value) {
    std::string result{"\""};
    for (const char byte : value) {
        switch (byte) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += byte; break;
        }
    }
    result += '"';
    return result;
}

std::string valueSource(const std::shared_ptr<Expr>& value, std::size_t depth) {
    constexpr std::size_t MaximumDepth = 64;
    if (depth > MaximumDepth)
        throw InterpreterError("fact database value nesting is too deep");
    if (!value || std::dynamic_pointer_cast<NilExpr>(value)) return "nil";
    if (const auto number = std::dynamic_pointer_cast<NumberExpr>(value))
        return numberSource(number->value);
    if (const auto text = std::dynamic_pointer_cast<StringExpr>(value))
        return quotedSource(text->value);
    if (const auto symbol = std::dynamic_pointer_cast<VarExpr>(value))
        return sourceName(symbol->name);
    if (const auto array = std::dynamic_pointer_cast<ArrayExpr>(value)) {
        std::ostringstream output;
        output << '[';
        for (std::size_t index = 0; index < array->items.size(); ++index) {
            if (index) output << ", ";
            output << valueSource(array->items[index], depth + 1);
        }
        output << ']';
        return output.str();
    }
    if (const auto map = std::dynamic_pointer_cast<MapExpr>(value)) {
        std::ostringstream output;
        output << '{';
        bool first = true;
        for (const auto& field : map->entries) {
            if (field.keyId == InternalSymbol::TypeId ||
                field.keyId == InternalSymbol::ParentId) continue;
            if (!first) output << ", ";
            first = false;
            output << sourceName(field.key) << ": "
                   << valueSource(field.value, depth + 1);
        }
        output << '}';
        return output.str();
    }
    throw InterpreterError("fact database contains a value that cannot be persisted");
}

std::vector<const MapEntry*> publicFields(const MapExpr& fact) {
    std::vector<const MapEntry*> result;
    result.reserve(fact.entries.size());
    for (const auto& field : fact.entries) {
        if (field.keyId != InternalSymbol::TypeId &&
            field.keyId != InternalSymbol::ParentId) result.push_back(&field);
    }
    return result;
}

std::string csvCell(const std::shared_ptr<Expr>& value) {
    std::string text;
    if (!value || std::dynamic_pointer_cast<NilExpr>(value)) {
        text.clear();
    } else if (const auto string = std::dynamic_pointer_cast<StringExpr>(value)) {
        text = string->value;
    } else if (const auto number = std::dynamic_pointer_cast<NumberExpr>(value)) {
        text = numberSource(number->value);
    } else if (const auto symbol = std::dynamic_pointer_cast<VarExpr>(value)) {
        text = symbol->name;
    } else {
        throw InterpreterError("CSV fact database supports only scalar field values");
    }
    if (text.find_first_of(",\"\n\r") == std::string::npos) return text;
    std::string quoted{"\""};
    for (const char byte : text) {
        if (byte == '"') quoted += "\"\"";
        else quoted += byte;
    }
    quoted += '"';
    return quoted;
}

const MapEntry* findField(const MapExpr& fact, SymbolId keyId,
                          std::string_view key) {
    const auto found = std::find_if(fact.entries.begin(), fact.entries.end(),
        [&](const MapEntry& field) {
            return field.keyId == keyId && field.key == key;
        });
    return found == fact.entries.end() ? nullptr : &*found;
}

std::string csvSource(const std::vector<std::shared_ptr<MapExpr>>& rows,
                      const std::shared_ptr<MapExpr>& emptySchema) {
    std::vector<std::pair<std::string, SymbolId>> columns;
    std::unordered_map<SymbolId, std::string> known;
    const auto includeColumns = [&](const MapExpr& row) {
        for (const auto* field : publicFields(row)) {
            const auto existing = known.find(field->keyId);
            if (existing != known.end()) {
                if (existing->second != field->key)
                    throw InterpreterError("CSV fact database contains a field identity collision");
                continue;
            }
            known.emplace(field->keyId, field->key);
            columns.emplace_back(field->key, field->keyId);
        }
    };
    for (const auto& row : rows) includeColumns(*row);
    if (rows.empty() && emptySchema) includeColumns(*emptySchema);

    std::ostringstream output;
    for (std::size_t index = 0; index < columns.size(); ++index) {
        if (index) output << ',';
        output << csvCell(std::make_shared<StringExpr>(columns[index].first));
    }
    output << "\r\n";
    for (const auto& row : rows) {
        for (std::size_t index = 0; index < columns.size(); ++index) {
            if (index) output << ',';
            const auto* field = findField(*row, columns[index].second,
                                          columns[index].first);
            output << csvCell(field ? field->value : std::shared_ptr<Expr>{});
        }
        output << "\r\n";
    }
    return output.str();
}

void atomicWrite(const fs::path& destination, std::string_view content) {
    fs::create_directories(fs::absolute("build/runtime"));
    if (destination.has_parent_path()) fs::create_directories(destination.parent_path());
    const auto token = temporarySequence.fetch_add(1, std::memory_order_relaxed);
    const auto hash = std::hash<std::string>{}(destination.string());
    const fs::path temporary = fs::absolute("build/runtime") /
        ("fact-write-" + std::to_string(hash) + "-" + std::to_string(token) + ".tmp");
    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                throw InterpreterError("cannot write temporary fact database: " + temporary.string());
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.flush();
            if (!output)
                throw InterpreterError("cannot complete temporary fact database: " + temporary.string());
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.wstring().c_str(), destination.wstring().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw InterpreterError("cannot replace fact database: " + destination.string());
        }
#else
        if (std::rename(temporary.c_str(), destination.c_str()) != 0) {
            throw InterpreterError("cannot replace fact database: " + destination.string());
        }
#endif
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

std::string persistenceFailure(std::string_view operation,
                               const fs::path& failed,
                               const std::vector<fs::path>& committed,
                               std::string_view detail) {
    std::ostringstream message;
    message << "fact " << operation << " persistence failed for " << failed.string();
    if (!committed.empty()) {
        message << " after committing";
        for (const auto& source : committed) message << ' ' << source.string();
    }
    message << ": " << detail;
    return message.str();
}

} // namespace

void Interpreter::persistFactSource(
    const fs::path& requestedSource,
    const std::shared_ptr<MapExpr>& emptySchema) {
    const fs::path source = normalizedSource(requestedSource);
    if (source.empty() || (source.extension() != ".csv" && source.extension() != ".fx")) {
        throw InterpreterError("fact persistence requires a .csv or .fx source");
    }
    if (source.extension() == ".fx" && fs::exists(source)) {
        bool factsOnly = true;
        parseProgramFileStatements(source, [&](std::shared_ptr<Statement> statement) {
            const auto clause = std::dynamic_pointer_cast<ClauseStmt>(statement);
            if (!clause || !clause->isFact()) factsOnly = false;
        });
        if (!factsOnly) {
            throw InterpreterError(
                "automatic persistence refuses to rewrite a non-fact-only .fx source: " +
                source.string());
        }
    }

    auto indexes = memory_.factIndexesFromOrigin(source);
    std::vector<std::pair<const FactRecord*, std::shared_ptr<MapExpr>>> records;
    records.reserve(indexes.size());
    for (const auto index : indexes) {
        const auto& record = memory_.fact(index);
        if (!record.active) continue;
        const auto value = memory_.factValue(index);
        if (value) records.emplace_back(&record, value);
    }
    std::stable_sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
        return left.first->id < right.first->id;
    });

    std::string content;
    if (source.extension() == ".csv") {
        std::vector<std::shared_ptr<MapExpr>> rows;
        rows.reserve(records.size());
        std::string type;
        for (const auto& [record, value] : records) {
            if (type.empty()) type = record->type;
            else if (record->type != type)
                throw InterpreterError("CSV fact database cannot contain multiple fact types");
            rows.push_back(value);
        }
        content = csvSource(rows, emptySchema);
    } else {
        std::ostringstream output;
        output << "# Felidae fact database\n";
        for (const auto& [record, value] : records) {
            output << '\n' << sourceName(record->type);
            if (!record->parentType.empty())
                output << " extend " << sourceName(record->parentType);
            output << '(';
            const auto fields = publicFields(*value);
            for (std::size_t index = 0; index < fields.size(); ++index) {
                if (index) output << ", ";
                output << sourceName(fields[index]->key) << ": "
                       << valueSource(fields[index]->value, 0);
            }
            output << ")\n";
        }
        content = output.str();
    }

    std::lock_guard lock(persistenceMutex);
    FactFileLock fileLock(source);
    atomicWrite(source, content);
}

std::shared_ptr<MapExpr> Interpreter::insertFact(
    const std::string& type,
    const MapExpr& values,
    const Env& env,
    const std::optional<fs::path>& requestedSource) {
    std::optional<fs::path> owner;
    if (requestedSource && !requestedSource->empty()) {
        owner = normalizedSource(*requestedSource);
    } else {
        std::set<fs::path> sources;
        for (const auto& source : memory_.originsForType(type))
            sources.insert(normalizedSource(source));
        if (sources.size() == 1) owner = *sources.begin();
        else if (sources.size() > 1) {
            throw InterpreterError(
                "Type.insert requires source when its fact type has multiple sources");
        }
    }

    auto inserted = prepareInsertedFact(type, values, env);
    FactMemory previous = memory_;
    memory_.addFact(type, {}, inserted, owner.value_or(fs::path{}));
    if (owner) {
        try {
            persistFactSource(*owner, inserted);
        } catch (...) {
            memory_ = std::move(previous);
            throw;
        }
    }
    return inserted;
}

std::shared_ptr<ArrayExpr> Interpreter::updateFacts(
    const std::shared_ptr<FactSelectionExpr>& selection,
    const MapExpr& values) {
    const auto rows = materializeFactSelection(selection);
    std::vector<std::uint64_t> orderedIds;
    std::vector<std::size_t> memoryIndexes;
    std::map<fs::path, std::vector<std::size_t>> sourceIndexes;
    std::unordered_map<std::uint64_t, std::shared_ptr<MapExpr>> updatedById;
    for (const auto& row : rows->items) {
        const auto fact = std::dynamic_pointer_cast<MapExpr>(row);
        if (!fact || fact->factIdentity == 0) continue;
        const auto index = memory_.factIndexById(fact->factIdentity);
        if (!index) continue;
        orderedIds.push_back(fact->factIdentity);
        const auto& record = memory_.fact(*index);
        if (record.origin.empty()) memoryIndexes.push_back(*index);
        else sourceIndexes[normalizedSource(record.origin)].push_back(*index);
    }

    const auto apply = [&](const std::vector<std::size_t>& indexes) {
        for (const auto index : indexes) {
            const auto current = memory_.factValue(index);
            // `index` was just resolved from the selection this same call is
            // updating, so both lookups below are expected to always succeed;
            // treat either failing as a real error rather than silently
            // dropping the row from the returned array, which would look
            // indistinguishable from "did not match" to the caller.
            if (!current) {
                throw InterpreterError("Fact.update could not read fact at index " +
                                       std::to_string(index) + " for update");
            }
            const auto identity = current->factIdentity;
            auto next = std::static_pointer_cast<MapExpr>(current->clone());
            for (const auto& field : values.entries) {
                const auto found = std::find_if(next->entries.begin(), next->entries.end(),
                    [&](const MapEntry& entry) { return entry.keyId == field.keyId; });
                if (found == next->entries.end()) {
                    next->entries.emplace_back(field.key, field.keyId,
                                               field.value ? field.value->clone() : std::make_shared<NilExpr>());
                } else {
                    found->value = field.value ? field.value->clone() : std::make_shared<NilExpr>();
                }
            }
            if (!memory_.replaceFact(index, next)) {
                throw InterpreterError("Fact.update failed to replace fact " +
                                       std::to_string(identity));
            }
            updatedById[identity] = std::move(next);
        }
    };

    {
        // Unlike the per-source batches below, an in-memory-only update has
        // no persisted file to roll back to, but memory_ itself must not be
        // left half-updated if apply() throws partway through this batch.
        FactMemory previous = memory_;
        try {
            apply(memoryIndexes);
        } catch (...) {
            memory_ = std::move(previous);
            throw;
        }
    }
    std::vector<fs::path> committed;
    for (const auto& [source, indexes] : sourceIndexes) {
        FactMemory previous = memory_;
        const auto emptySchema = indexes.empty() ? std::shared_ptr<MapExpr>{}
                                                  : memory_.factValue(indexes.front());
        try {
            apply(indexes);
            persistFactSource(source, emptySchema);
        } catch (const std::exception& error) {
            memory_ = std::move(previous);
            throw InterpreterError(persistenceFailure(
                "update", source, committed, error.what()));
        }
        committed.push_back(source);
    }

    std::vector<std::shared_ptr<Expr>> updated;
    updated.reserve(orderedIds.size());
    for (const auto id : orderedIds) {
        const auto found = updatedById.find(id);
        if (found != updatedById.end()) updated.push_back(found->second);
    }
    return std::make_shared<ArrayExpr>(std::move(updated));
}

double Interpreter::deleteFacts(
    const std::shared_ptr<FactSelectionExpr>& selection) {
    const auto rows = materializeFactSelection(selection);
    std::vector<std::size_t> memoryIndexes;
    std::map<fs::path, std::vector<std::size_t>> sourceIndexes;
    for (const auto& row : rows->items) {
        const auto fact = std::dynamic_pointer_cast<MapExpr>(row);
        if (!fact || fact->factIdentity == 0) continue;
        const auto index = memory_.factIndexById(fact->factIdentity);
        if (!index) continue;
        const auto& record = memory_.fact(*index);
        if (record.origin.empty()) memoryIndexes.push_back(*index);
        else sourceIndexes[normalizedSource(record.origin)].push_back(*index);
    }

    std::size_t deleted = memory_.deactivateFacts(memoryIndexes);
    std::vector<fs::path> committed;
    for (const auto& [source, indexes] : sourceIndexes) {
        FactMemory previous = memory_;
        const auto emptySchema = indexes.empty() ? std::shared_ptr<MapExpr>{}
                                                  : memory_.factValue(indexes.front());
        try {
            deleted += memory_.deactivateFacts(indexes);
            persistFactSource(source, emptySchema);
        } catch (const std::exception& error) {
            memory_ = std::move(previous);
            throw InterpreterError(persistenceFailure(
                "deletion", source, committed, error.what()));
        }
        committed.push_back(source);
    }
    return static_cast<double>(deleted);
}

} // namespace Felidae
