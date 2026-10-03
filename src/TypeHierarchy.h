#pragma once

#include "Symbol.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Felidae {

// The runtime type lineage: class and fact inheritance as a directed acyclic
// graph. It lives in interpreter memory and is rebuilt from the parent edges
// persisted in RocksDB when a database is opened. Fact rows are never held
// here; they exist only in RocksDB. Variables and other runtime objects live
// in the interpreter's environments, not in this class.
class TypeHierarchy {
public:
    // Adds `parent` as a parent of `child`. A child's first parent is its
    // primary parent; later ones are additional. Re-adding an existing edge is
    // a no-op, and an edge that would close a cycle throws std::invalid_argument.
    void setParent(const std::string& child, const std::string& parent);

    // Primary parent of each child. Additional parents are not listed here.
    const std::unordered_map<std::string, std::string>& parents() const {
        return parentOf_;
    }
    // All parents of a type, primary first.
    std::vector<std::string> parentsOf(const std::string& child) const;
    const std::vector<SymbolId>& parentsOf(SymbolId childId) const;
    // Every (child, parent) edge, primary and additional.
    std::vector<std::pair<std::string, std::string>> hierarchyEdges() const;

    // True when `actual` is `expected` or inherits from it, directly or
    // transitively. Every type is compatible with "Fact".
    bool isCompatibleType(const std::string& actual, const std::string& expected) const;

    // Advances whenever an edge is added, so caches derived from the lineage
    // can detect that it changed.
    std::uint64_t hierarchyGeneration() const { return hierarchyGeneration_; }

private:
    std::unordered_map<std::string, std::string> parentOf_;
    std::unordered_map<std::string, std::vector<std::string>> additionalParentsOf_;
    std::unordered_map<SymbolId, std::vector<SymbolId>> parentsByChild_;
    std::uint64_t hierarchyGeneration_ = 1;
};

} // namespace Felidae
