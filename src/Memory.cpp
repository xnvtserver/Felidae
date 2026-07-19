#include "Memory.h"

#include <set>
#include <utility>

namespace Felidae {

const std::vector<FactRecord>& FactMemory::facts() const {
    return facts_;
}

const FactRecord& FactMemory::fact(size_t index) const {
    return facts_.at(index);
}

size_t FactMemory::addFact(std::string type,
                           std::string parentType,
                           std::shared_ptr<MapExpr> value,
                           std::filesystem::path origin) {
    facts_.push_back(FactRecord{std::move(type), std::move(parentType), std::move(value), std::move(origin)});
    const size_t index = facts_.size() - 1;
    factsByType_[facts_[index].type].push_back(index);
    if (!facts_[index].origin.empty()) factsByOrigin_[facts_[index].origin].push_back(index);
    compatibleFactCache_.clear();
    return index;
}

void FactMemory::setParent(std::string child, std::string parent) {
    parentOf_[std::move(child)] = std::move(parent);
    compatibleFactCache_.clear();
}

void FactMemory::setParent(std::string child, std::string parent, std::filesystem::path origin) {
    std::string childName = std::move(child);
    setParent(childName, std::move(parent));
    if (!origin.empty()) parentOrigin_[std::move(childName)] = std::move(origin);
}

const std::unordered_map<std::string, std::string>& FactMemory::parents() const {
    return parentOf_;
}

const std::unordered_map<std::string, std::filesystem::path>& FactMemory::parentOrigins() const {
    return parentOrigin_;
}

std::vector<size_t> FactMemory::factIndexesFromOrigin(const std::filesystem::path& origin) const {
    auto found = factsByOrigin_.find(origin);
    if (found == factsByOrigin_.end()) return {};
    return found->second;
}

bool FactMemory::hasOrigin(const std::filesystem::path& origin) const {
    return factsByOrigin_.count(origin) > 0;
}

bool FactMemory::isCompatibleType(const std::string& actual, const std::string& expected) const {
    if (actual == expected) return true;
    std::set<std::string> seen;
    std::string current = actual;
    while (!current.empty() && !seen.count(current)) {
        seen.insert(current);
        std::string parent;
        auto parentIt = parentOf_.find(current);
        if (parentIt != parentOf_.end()) parent = parentIt->second;
        if (parent == expected) return true;
        current = parent;
    }
    return false;
}

const std::vector<size_t>& FactMemory::compatibleFactIndexes(const std::string& type) {
    auto cached = compatibleFactCache_.find(type);
    if (cached != compatibleFactCache_.end()) return cached->second;

    std::vector<size_t> indexes;
    auto direct = factsByType_.find(type);
    if (direct != factsByType_.end()) {
        indexes.insert(indexes.end(), direct->second.begin(), direct->second.end());
    }
    for (size_t i = 0; i < facts_.size(); ++i) {
        if (facts_[i].type != type && isCompatibleType(facts_[i].type, type)) {
            indexes.push_back(i);
        }
    }
    auto inserted = compatibleFactCache_.emplace(type, std::move(indexes));
    return inserted.first->second;
}

void FactMemory::invalidateCaches() {
    compatibleFactCache_.clear();
}

} // namespace Felidae
