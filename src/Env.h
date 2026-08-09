#pragma once

#include "AST.h"
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
class Env {
public:
    using Map = std::unordered_map<SymbolId, std::shared_ptr<Expr>>;
    using key_type = Map::key_type;
    using mapped_type = Map::mapped_type;
    using value_type = Map::value_type;
    using iterator = Map::iterator;
    using const_iterator = Map::const_iterator;

    Env() = default;
    Env(const Env&) = default;
    Env(Env&&) noexcept = default;
    Env& operator=(const Env&) = default;
    Env& operator=(Env&&) noexcept = default;

    std::size_t count(const std::string& name) const {
        return values_.count(symbolIdForName(name));
    }
    iterator find(const std::string& name) {
        return values_.find(symbolIdForName(name));
    }
    const_iterator find(const std::string& name) const {
        return values_.find(symbolIdForName(name));
    }
    iterator find(SymbolId id) { return values_.find(id); }
    const_iterator find(SymbolId id) const { return values_.find(id); }
    iterator begin() { return values_.begin(); }
    const_iterator begin() const { return values_.begin(); }
    iterator end() { return values_.end(); }
    const_iterator end() const { return values_.end(); }
    std::shared_ptr<Expr>& operator[](const std::string& name) {
        return values_[symbolIdForName(name)];
    }
    std::shared_ptr<Expr>& operator[](SymbolId id) {
        return values_[id];
    }
    std::size_t erase(const std::string& name) {
        return values_.erase(symbolIdForName(name));
    }
    std::size_t erase(SymbolId id) {
        return values_.erase(id);
    }
    void clear() { values_.clear(); }
    void reserve(std::size_t size) { values_.reserve(size); }
    template <typename Iterator>
    void insert(Iterator begin, Iterator end) {
        values_.insert(begin, end);
    }
    std::size_t size() const { return values_.size(); }
    std::size_t bucket_count() const { return values_.bucket_count(); }

private:
    friend class BindingTrail;
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

class EnvFramePool;

class EnvFrame {
public:
    EnvFrame() = default;
    EnvFrame(EnvFramePool* pool, Env* env);
    EnvFrame(const EnvFrame&) = delete;
    EnvFrame& operator=(const EnvFrame&) = delete;
    EnvFrame(EnvFrame&& other) noexcept;
    EnvFrame& operator=(EnvFrame&& other) noexcept;
    ~EnvFrame();

    Env& get();
    const Env& get() const;
    Env* operator->();
    Env& operator*();
    explicit operator bool() const;
    void reset();

private:
    EnvFramePool* pool_ = nullptr;
    Env* env_ = nullptr;
};

class EnvFramePool {
public:
    EnvFrame acquire();
    EnvFrame acquireCopy(const Env& source);
    EnvFrame acquireMove(Env&& source);
    void recycle(Env* env);
    void collectGarbage(std::size_t maxCachedFrames);
    std::size_t created() const;
    std::size_t cached() const;
    std::size_t copies() const;

private:
    std::vector<std::unique_ptr<Env>> free_;
    std::size_t created_ = 0;
    std::size_t copies_ = 0;
};

} // namespace Felidae
