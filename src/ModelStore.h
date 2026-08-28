#pragma once

#include "form/RegisterVm.h"

#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Felidae {

// Shared by every versioned model loader (compiler-side mixfix GRU, VM-side
// runtime GRU, and any future model). A manifest is a flat `key=value` text
// file written beside its `.pt` artifact; this is the one authoritative
// reader for that format. Do not reintroduce a second copy in a model's own
// .cpp -- the compiler and VM SSM loaders duplicated this by hand until both
// were folded into this function.
inline std::string manifestValue(const std::filesystem::path& manifestPath,
                                 const std::string& wanted) {
    std::ifstream manifest(manifestPath);
    if (!manifest)
        throw IrError("model manifest is unavailable: " + manifestPath.string());
    std::string line;
    while (std::getline(manifest, line)) {
        const auto separator = line.find('=');
        if (separator != std::string::npos && line.substr(0, separator) == wanted) {
            return line.substr(separator + 1);
        }
    }
    throw IrError("model manifest omits " + wanted);
}

// Training artifacts have two intentional destinations.  Keeping the model in
// a named subdirectory prevents the executable and model files from being
// mixed at the root of build/ or dist/.
inline std::filesystem::path modelStoreDirectory(const std::filesystem::path& requested,
                                                 std::string_view modelName) {
    namespace fs = std::filesystem;
    const auto current = fs::current_path().lexically_normal();
    auto root = current;
    // Training is commonly launched either from the repository root or from
    // build/<configuration>. Resolve the existing source root without
    // creating another nested build directory.
    for (auto candidate = current;; candidate = candidate.parent_path()) {
        if (fs::is_regular_file(candidate / "CMakeLists.txt") &&
            fs::is_directory(candidate / "build")) {
            root = candidate;
            break;
        }
        if (candidate == candidate.root_path() || candidate.parent_path() == candidate) break;
    }
    const auto build = (root / "build").lexically_normal();
    const auto dist = (root / "dist").lexically_normal();
    const auto selected = (requested.is_absolute() ? requested : (root / requested)).lexically_normal();
    if (selected == build) return build / modelName;
    if (selected == dist) return dist / "models" / modelName;
    throw std::runtime_error("--store-model accepts only build or dist");
}

// Windows shells do not expand wildcards for native executables.  Keep the
// expansion here so `--train datasets/compiler/*.jsonl` is portable across
// the supported CLI hosts and does not require a scratch merge file.
inline bool wildcardMatches(std::string_view pattern, std::string_view text) {
    std::size_t patternIndex = 0, textIndex = 0, star = std::string_view::npos, retry = 0;
    while (textIndex < text.size()) {
        if (patternIndex < pattern.size() && pattern[patternIndex] == text[textIndex]) {
            ++patternIndex;
            ++textIndex;
        } else if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
            star = patternIndex++;
            retry = textIndex;
        } else if (star != std::string_view::npos) {
            patternIndex = star + 1;
            textIndex = ++retry;
        } else {
            return false;
        }
    }
    while (patternIndex < pattern.size() && pattern[patternIndex] == '*') ++patternIndex;
    return patternIndex == pattern.size();
}

inline std::vector<std::filesystem::path> expandJsonlDatasetPaths(const std::filesystem::path& requested) {
    namespace fs = std::filesystem;
    const auto filename = requested.filename().string();
    if (filename.find('*') == std::string::npos) {
        if (requested.extension() != ".jsonl" || !fs::is_regular_file(requested)) {
            throw std::runtime_error("--train requires an existing .jsonl dataset or .jsonl wildcard");
        }
        return {requested};
    }
    if (requested.extension() != ".jsonl") {
        throw std::runtime_error("--train wildcard must select .jsonl datasets");
    }
    const auto directory = requested.has_parent_path() ? requested.parent_path() : fs::path{"."};
    if (!fs::is_directory(directory)) throw std::runtime_error("training dataset directory does not exist: " + directory.string());
    std::vector<fs::path> result;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && wildcardMatches(filename, entry.path().filename().string())) {
            result.push_back(entry.path());
        }
    }
    std::sort(result.begin(), result.end());
    if (result.empty()) throw std::runtime_error("--train wildcard matched no .jsonl datasets");
    return result;
}

inline std::string encodeDatasetPaths(const std::vector<std::filesystem::path>& paths) {
    // U+001F cannot occur in a Windows filename. The trainer keeps the list
    // in memory; no temporary/merged dataset is written.
    constexpr char separator = '\x1f';
    std::string result;
    for (const auto& path : paths) {
        if (path.string().find(separator) != std::string::npos) throw std::runtime_error("training dataset path contains a reserved separator");
        if (!result.empty()) result.push_back(separator);
        result += path.string();
    }
    return result;
}

struct ModelTrainingOptions {
    std::filesystem::path dataset;
    std::filesystem::path store;
    std::size_t epochs = 8;
    double learningRate = 0.001;
};

// Shared only by the two production model commands.  Both keep their model
// architecture-specific defaults behind their existing trainers.
inline std::optional<ModelTrainingOptions> parseModelTrainingOptions(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "--train") return std::nullopt;
    if (argc < 4) throw std::runtime_error("--train requires a dataset and --store-model build|dist");
    ModelTrainingOptions options;
    options.dataset = argv[2];
    bool haveStore = false, haveEpochs = false, haveLearningRate = false;
    const auto positiveSize = [](std::string_view text, const char* option) {
        if (text.empty() || text.front() == '-') throw std::runtime_error(std::string(option) + " requires a positive integer");
        std::size_t consumed = 0;
        const auto value = std::stoull(std::string(text), &consumed);
        if (consumed != text.size() || value == 0 || value > std::numeric_limits<std::size_t>::max()) {
            throw std::runtime_error(std::string(option) + " requires a positive integer in range");
        }
        return static_cast<std::size_t>(value);
    };
    const auto positiveReal = [](std::string_view text, const char* option) {
        std::size_t consumed = 0;
        const auto value = std::stod(std::string(text), &consumed);
        if (consumed != text.size() || !std::isfinite(value) || value <= 0.0) {
            throw std::runtime_error(std::string(option) + " requires a positive finite number");
        }
        return value;
    };
    for (int index = 3; index < argc; ++index) {
        const std::string_view flag(argv[index]);
        if (flag == "--store-model") {
            if (haveStore || ++index == argc) throw std::runtime_error("--store-model requires one directory");
            options.store = argv[index];
            haveStore = true;
        } else if (flag == "--epochs") {
            if (haveEpochs || ++index == argc) throw std::runtime_error("--epochs requires one positive integer");
            options.epochs = positiveSize(argv[index], "--epochs");
            haveEpochs = true;
        } else if (flag == "--learning-rate") {
            if (haveLearningRate || ++index == argc) throw std::runtime_error("--learning-rate requires one positive number");
            options.learningRate = positiveReal(argv[index], "--learning-rate");
            haveLearningRate = true;
        } else {
            throw std::runtime_error("unknown training option: " + std::string(flag));
        }
    }
    if (!haveStore) {
        throw std::runtime_error("training options are invalid");
    }
    return options;
}

} // namespace Felidae
