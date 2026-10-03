#pragma once

#include "AST.h"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Felidae {

// Runtime bindings are keyed by collision-free SymbolId rather than repeated
// heap strings. Source spellings stay on AST nodes; binding lookup converts a
// spelling once through the shared symbol interner.
//
// The bindings are one vector sorted by id. A call copies its caller's
// environment, and with a node-based hash map that copy cost one allocation per
// binding; here it is a single allocation. Lookup is a binary search, so large
// environments (top-level globals) stay fast too.
//
// Invalidation rule: any insertion or erasure on an Env invalidates every
// reference and iterator into it (operator[] inserts when the id is absent).
// Never hold one across a write to the same Env.
class Env {
public:
    using key_type = SymbolId;
    using mapped_type = std::shared_ptr<Expr>;
    using value_type = std::pair<SymbolId, std::shared_ptr<Expr>>;
    using Map = std::vector<value_type>;
    using iterator = Map::iterator;
    using const_iterator = Map::const_iterator;

    Env() = default;
    Env(const Env&) = default;
    Env(Env&&) noexcept = default;
    Env& operator=(const Env&) = default;
    Env& operator=(Env&&) noexcept = default;

    std::size_t count(const std::string& name) const {
        return count(symbolIdForName(name));
    }
    std::size_t count(SymbolId id) const { return find(id) != end() ? 1 : 0; }
    iterator find(const std::string& name) { return find(symbolIdForName(name)); }
    const_iterator find(const std::string& name) const { return find(symbolIdForName(name)); }
    iterator find(SymbolId id) {
        const auto position = lowerBound(id);
        return position != values_.end() && position->first == id ? position : values_.end();
    }
    const_iterator find(SymbolId id) const {
        const auto position = lowerBound(id);
        return position != values_.end() && position->first == id ? position : values_.end();
    }
    iterator begin() { return values_.begin(); }
    const_iterator begin() const { return values_.begin(); }
    iterator end() { return values_.end(); }
    const_iterator end() const { return values_.end(); }
    std::shared_ptr<Expr>& operator[](const std::string& name) {
        return (*this)[symbolIdForName(name)];
    }
    std::shared_ptr<Expr>& operator[](SymbolId id) {
        auto position = lowerBound(id);
        if (position == values_.end() || position->first != id) {
            position = values_.emplace(position, id, nullptr);
        }
        return position->second;
    }
    std::size_t erase(const std::string& name) { return erase(symbolIdForName(name)); }
    std::size_t erase(SymbolId id) {
        const auto position = find(id);
        if (position == values_.end()) return 0;
        values_.erase(position);
        return 1;
    }
    void clear() { values_.clear(); }
    void reserve(std::size_t size) { values_.reserve(size); }
    template <typename Iterator>
    void insert(Iterator first, Iterator last) {
        for (; first != last; ++first) (*this)[first->first] = first->second;
    }
    std::size_t size() const { return values_.size(); }

private:
    iterator lowerBound(SymbolId id) {
        return std::lower_bound(values_.begin(), values_.end(), id,
            [](const value_type& entry, SymbolId key) { return entry.first < key; });
    }
    const_iterator lowerBound(SymbolId id) const {
        return std::lower_bound(values_.begin(), values_.end(), id,
            [](const value_type& entry, SymbolId key) { return entry.first < key; });
    }

    Map values_;
};

// Reversible bindings for recursive search. Values remain shared and
// immutable; only changed symbol slots are recorded.
class BindingTrail {
public:
    using Checkpoint = std::size_t;

    Checkpoint checkpoint() const { return entries_.size(); }
    void assign(Env& env, SymbolId id, std::shared_ptr<Expr> value);
    void rollback(Checkpoint checkpoint);

private:
    struct Entry {
        Env* env = nullptr;
        SymbolId id = 0;
        bool existed = false;
        std::shared_ptr<Expr> previous;
    };
    std::vector<Entry> entries_;
};

class GlobalEnv {
public:
    using Map = Env;
    using iterator = Map::iterator;
    using const_iterator = Map::const_iterator;

    size_t count(const std::string& name) const;
    size_t count(SymbolId id) const;
    iterator find(const std::string& name);
    iterator find(SymbolId id);
    const_iterator find(const std::string& name) const;
    const_iterator find(SymbolId id) const;
    iterator end();
    const_iterator end() const;
    iterator begin();
    const_iterator begin() const;
    std::shared_ptr<Expr>& operator[](const std::string& name);
    std::shared_ptr<Expr>& operator[](SymbolId id);
    void bind(const std::string& name,
              const std::shared_ptr<Expr>& value,
              std::filesystem::path origin = {});
    void setOrigin(const std::string& name, std::filesystem::path origin);
    void erase(const std::string& name);
    void eraseOrigin(const std::filesystem::path& origin);
    void replaceValues(Env values);
    const Map& values() const;

private:
    Map values_;
    std::unordered_map<SymbolId, std::filesystem::path> origins_;
};

struct Solution {
    Env env;
};

Env cloneEnv(const Env& env);
std::shared_ptr<Expr> findEnvValue(const Env& env, const std::string& name);
std::shared_ptr<Expr> findReturnValue(const Env& env);
bool bindEnvValue(Env& env, const std::string& name, const std::shared_ptr<Expr>& value);

} // namespace Felidae
