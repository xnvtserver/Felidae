#include "Env.h"
#include "Symbol.h"

namespace Felidae {

void BindingTrail::assign(Env& env, SymbolId id, std::shared_ptr<Expr> value) {
    const auto found = env.find(id);
    const bool existed = found != env.end();
    entries_.push_back(Entry{&env, id, existed, existed ? found->second : nullptr});
    env[id] = std::move(value);
}

void BindingTrail::rollback(Checkpoint checkpoint) {
    while (entries_.size() > checkpoint) {
        Entry entry = std::move(entries_.back());
        entries_.pop_back();
        if (!entry.env) continue;
        if (entry.existed) (*entry.env)[entry.id] = std::move(entry.previous);
        else entry.env->erase(entry.id);
    }
}

Env cloneEnv(const Env& env) {
    // Runtime values are immutable. A branch needs its own binding directory,
    // not deep copies of every bound value graph.
    return env;
}

std::shared_ptr<Expr> findEnvValue(const Env& env, const std::string& name) {
    auto found = env.find(name);
    if (found == env.end() || !found->second) return nullptr;
    return found->second;
}

std::shared_ptr<Expr> findReturnValue(const Env& env) {
    const auto found = env.find(InternalSymbol::ReturnId);
    return found == env.end() ? nullptr : found->second;
}

bool bindEnvValue(Env& env, const std::string& name, const std::shared_ptr<Expr>& value) {
    if (!value) return false;
    env[name] = value;
    return true;
}

size_t GlobalEnv::count(const std::string& name) const {
    return values_.count(name);
}

size_t GlobalEnv::count(SymbolId id) const { return values_.find(id) != values_.end(); }

GlobalEnv::iterator GlobalEnv::find(const std::string& name) {
    return values_.find(name);
}

GlobalEnv::iterator GlobalEnv::find(SymbolId id) { return values_.find(id); }

GlobalEnv::const_iterator GlobalEnv::find(const std::string& name) const {
    return values_.find(name);
}

GlobalEnv::const_iterator GlobalEnv::find(SymbolId id) const { return values_.find(id); }

GlobalEnv::iterator GlobalEnv::end() {
    return values_.end();
}

GlobalEnv::const_iterator GlobalEnv::end() const {
    return values_.end();
}

GlobalEnv::iterator GlobalEnv::begin() {
    return values_.begin();
}

GlobalEnv::const_iterator GlobalEnv::begin() const {
    return values_.begin();
}

std::shared_ptr<Expr>& GlobalEnv::operator[](const std::string& name) {
    return values_[name];
}

std::shared_ptr<Expr>& GlobalEnv::operator[](SymbolId id) { return values_[id]; }

void GlobalEnv::bind(const std::string& name,
                     const std::shared_ptr<Expr>& value,
                     std::filesystem::path origin) {
    values_[name] = value ? value->clone() : nullptr;
    if (!origin.empty()) origins_[symbolIdForName(name)] = std::move(origin);
}

void GlobalEnv::setOrigin(const std::string& name, std::filesystem::path origin) {
    if (origin.empty()) return;
    origins_[symbolIdForName(name)] = std::move(origin);
}

void GlobalEnv::erase(const std::string& name) {
    values_.erase(name);
    origins_.erase(symbolIdForName(name));
}

void GlobalEnv::eraseOrigin(const std::filesystem::path& origin) {
    if (origin.empty()) return;
    for (auto it = origins_.begin(); it != origins_.end();) {
        if (it->second == origin) {
            values_.erase(it->first);
            it = origins_.erase(it);
        } else {
            ++it;
        }
    }
}

void GlobalEnv::replaceValues(Env values) {
    values_ = std::move(values);
    origins_.clear();
}

const GlobalEnv::Map& GlobalEnv::values() const {
    return values_;
}

} // namespace Felidae
