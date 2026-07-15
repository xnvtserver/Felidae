#pragma once

#include "AST.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Felidae {

struct FactRecord {
    std::string type;
    std::string parentType;
    std::shared_ptr<MapExpr> value;
};

class FactMemory {
public:
    const std::vector<FactRecord>& facts() const;
    const FactRecord& fact(size_t index) const;

    size_t addFact(std::string type, std::string parentType, std::shared_ptr<MapExpr> value);
    void setParent(std::string child, std::string parent);
    const std::unordered_map<std::string, std::string>& parents() const;
    bool isCompatibleType(const std::string& actual, const std::string& expected) const;
    const std::vector<size_t>& compatibleFactIndexes(const std::string& type);
    void invalidateCaches();

private:
    std::vector<FactRecord> facts_;
    std::unordered_map<std::string, std::vector<size_t>> factsByType_;
    std::unordered_map<std::string, std::vector<size_t>> compatibleFactCache_;
    std::unordered_map<std::string, std::string> parentOf_;
};

} // namespace Felidae
