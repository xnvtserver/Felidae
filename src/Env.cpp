#include "Env.h"
#include "Symbol.h"

namespace Felidae {

namespace {

constexpr std::size_t kMaxPooledEnvBuckets = 4096;

}

Env cloneEnv(const Env& env) {
    Env cloned;
    cloned.reserve(env.size());
    for (const auto& entry : env) {
        cloned[entry.first] = entry.second ? entry.second->clone() : nullptr;
    }
    return cloned;
}

std::shared_ptr<Expr> findEnvValue(const Env& env, const std::string& name) {
    auto found = env.find(name);
    if (found == env.end() || !found->second) return nullptr;
    return found->second;
}

std::shared_ptr<Expr> findReturnValue(const Env& env) {
    return findEnvValue(env, internalSymbolString(InternalSymbolKind::Return));
}

bool bindEnvValue(Env& env, const std::string& name, const std::shared_ptr<Expr>& value) {
    if (!value) return false;
    env[name] = value->clone();
    return true;
}

size_t GlobalEnv::count(const std::string& name) const {
    return values_.count(name);
}

GlobalEnv::iterator GlobalEnv::find(const std::string& name) {
    return values_.find(name);
}

GlobalEnv::const_iterator GlobalEnv::find(const std::string& name) const {
    return values_.find(name);
}

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

void GlobalEnv::bind(const std::string& name,
                     const std::shared_ptr<Expr>& value,
                     std::filesystem::path origin) {
    values_[name] = value ? value->clone() : nullptr;
    if (!origin.empty()) origins_[name] = std::move(origin);
}

void GlobalEnv::setOrigin(const std::string& name, std::filesystem::path origin) {
    if (origin.empty()) return;
    origins_[name] = std::move(origin);
}

void GlobalEnv::erase(const std::string& name) {
    values_.erase(name);
    origins_.erase(name);
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

EnvFrame::EnvFrame(EnvFramePool* pool, Env* env) : pool_(pool), env_(env) {}

EnvFrame::EnvFrame(EnvFrame&& other) noexcept : pool_(other.pool_), env_(other.env_) {
    other.pool_ = nullptr;
    other.env_ = nullptr;
}

EnvFrame& EnvFrame::operator=(EnvFrame&& other) noexcept {
    if (this != &other) {
        reset();
        pool_ = other.pool_;
        env_ = other.env_;
        other.pool_ = nullptr;
        other.env_ = nullptr;
    }
    return *this;
}

EnvFrame::~EnvFrame() {
    reset();
}

Env& EnvFrame::get() {
    return *env_;
}

const Env& EnvFrame::get() const {
    return *env_;
}

Env* EnvFrame::operator->() {
    return env_;
}

Env& EnvFrame::operator*() {
    return *env_;
}

EnvFrame::operator bool() const {
    return env_ != nullptr;
}

void EnvFrame::reset() {
    if (pool_ && env_) {
        pool_->recycle(env_);
    }
    pool_ = nullptr;
    env_ = nullptr;
}

EnvFrame EnvFramePool::acquire() {
    if (free_.empty()) {
        ++created_;
        return EnvFrame(this, new Env());
    }
    std::unique_ptr<Env> env = std::move(free_.back());
    free_.pop_back();
    return EnvFrame(this, env.release());
}

EnvFrame EnvFramePool::acquireCopy(const Env& source) {
    ++copies_;
    EnvFrame frame = acquire();
    Env& env = frame.get();
    env.clear();
    env.reserve(source.size());
    env.insert(source.begin(), source.end());
    return frame;
}

EnvFrame EnvFramePool::acquireMove(Env&& source) {
    EnvFrame frame = acquire();
    frame.get() = std::move(source);
    return frame;
}

void EnvFramePool::recycle(Env* env) {
    if (!env) return;
    env->clear();
    if (env->bucket_count() > kMaxPooledEnvBuckets) {
        delete env;
        return;
    }
    free_.push_back(std::unique_ptr<Env>(env));
}

void EnvFramePool::collectGarbage(std::size_t maxCachedFrames) {
    while (free_.size() > maxCachedFrames) {
        free_.pop_back();
    }
}

std::size_t EnvFramePool::created() const {
    return created_;
}

std::size_t EnvFramePool::cached() const {
    return free_.size();
}

std::size_t EnvFramePool::copies() const {
    return copies_;
}

} // namespace Felidae
