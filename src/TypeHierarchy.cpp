#include "TypeHierarchy.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace Felidae {

void TypeHierarchy::setParent(const std::string& child, const std::string& parent) {
    if (child.empty() || parent.empty()) return;
    const auto existing = parentsOf(child);
    if (std::find(existing.begin(), existing.end(), parent) != existing.end()) return;
    if (child == parent || isCompatibleType(parent, child)) {
        throw std::invalid_argument("Inheritance cycle: '" + child + "' cannot extend '" + parent + "'");
    }
    const SymbolId childId = symbolIdForName(child);
    const SymbolId parentId = symbolIdForName(parent);
    if (!parentOf_.count(child)) parentOf_[child] = parent;
    else additionalParentsOf_[child].push_back(parent);
    parentsByChild_[childId].push_back(parentId);
    ++hierarchyGeneration_;
}

std::vector<std::string> TypeHierarchy::parentsOf(const std::string& child) const {
    std::vector<std::string> result;
    const auto primary = parentOf_.find(child);
    if (primary != parentOf_.end()) result.push_back(primary->second);
    const auto additional = additionalParentsOf_.find(child);
    if (additional != additionalParentsOf_.end()) {
        result.insert(result.end(), additional->second.begin(), additional->second.end());
    }
    return result;
}

const std::vector<SymbolId>& TypeHierarchy::parentsOf(SymbolId childId) const {
    static const std::vector<SymbolId> empty;
    const auto found = parentsByChild_.find(childId);
    return found == parentsByChild_.end() ? empty : found->second;
}

std::vector<std::pair<std::string, std::string>> TypeHierarchy::hierarchyEdges() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& entry : parentOf_) result.emplace_back(entry.first, entry.second);
    for (const auto& entry : additionalParentsOf_) {
        for (const auto& parent : entry.second) result.emplace_back(entry.first, parent);
    }
    return result;
}

bool TypeHierarchy::isCompatibleType(const std::string& actual, const std::string& expected) const {
    const SymbolId actualId = symbolIdForName(actual);
    const SymbolId expectedId = symbolIdForName(expected);
    if (actualId == 0 || expectedId == 0) return false;
    if (expectedId == symbolIdForName("Fact")) return true;
    if (actualId == expectedId) return true;
    std::unordered_set<SymbolId> seen;
    std::vector<SymbolId> pending{actualId};
    for (size_t index = 0; index < pending.size(); ++index) {
        // Copy before appending parents: vector growth must not invalidate the
        // current node while multi-parent traversal is expanding its frontier.
        const SymbolId current = pending[index];
        if (!seen.insert(current).second) continue;
        const auto parents = parentsByChild_.find(current);
        if (parents == parentsByChild_.end()) continue;
        for (const SymbolId parent : parents->second) {
            if (parent == expectedId) return true;
            pending.push_back(parent);
        }
    }
    return false;
}

} // namespace Felidae
