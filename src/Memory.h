#pragma once

#include "AST.h"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Felidae {

struct FactRecord {
    std::string type;
    std::string parentType;
    std::shared_ptr<MapExpr> value;
    std::filesystem::path origin;
};

class FactMemory {
public:
    const std::vector<FactRecord>& facts() const;
    const FactRecord& fact(size_t index) const;

    size_t addFact(std::string type,
                   std::string parentType,
                   std::shared_ptr<MapExpr> value,
                   std::filesystem::path origin = {});
    void setParent(std::string child, std::string parent);
    void setParent(std::string child, std::string parent, std::filesystem::path origin);
    const std::unordered_map<std::string, std::string>& parents() const;
    const std::unordered_map<std::string, std::filesystem::path>& parentOrigins() const;
    std::vector<size_t> factIndexesFromOrigin(const std::filesystem::path& origin) const;
    bool hasOrigin(const std::filesystem::path& origin) const;
    void removeOrigin(const std::filesystem::path& origin);
    bool isCompatibleType(const std::string& actual, const std::string& expected) const;
    const std::vector<size_t>& compatibleFactIndexes(const std::string& type);
    void invalidateCaches();

private:
    std::vector<FactRecord> facts_;
    std::unordered_map<std::string, std::vector<size_t>> factsByType_;
    std::unordered_map<std::filesystem::path, std::vector<size_t>> factsByOrigin_;
    std::unordered_map<std::string, std::vector<size_t>> compatibleFactCache_;
    std::unordered_map<std::string, std::string> parentOf_;
    std::unordered_map<std::string, std::filesystem::path> parentOrigin_;

    void rebuildIndexes();
};

} // namespace Felidae
