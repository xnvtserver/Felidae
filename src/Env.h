#pragma once

#include "AST.h"
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Felidae {

using Env = std::unordered_map<std::string, std::shared_ptr<Expr>>;

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

private:
    std::vector<std::unique_ptr<Env>> free_;
    std::size_t created_ = 0;
};

} // namespace Felidae
