#pragma once

#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Felidae {

// Training artifacts have two intentional destinations.  Keeping the model in
// a named subdirectory prevents the executable and model files from being
// mixed at the root of build/ or dist/.
inline std::filesystem::path modelStoreDirectory(const std::filesystem::path& requested,
                                                 std::string_view modelName) {
    namespace fs = std::filesystem;
    const auto current = fs::current_path().lexically_normal();
    const auto root = current.filename() == "build" ? current.parent_path() : current;
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
    bool haveStore = false;
    for (int index = 3; index < argc; ++index) {
        const std::string_view flag(argv[index]);
        if (flag == "--store-model") {
            if (haveStore || ++index == argc) throw std::runtime_error("--store-model requires one directory");
            options.store = argv[index];
            haveStore = true;
        } else if (flag == "--epochs") {
            if (++index == argc) throw std::runtime_error("--epochs requires a positive integer");
            const auto epochs = std::stoull(argv[index]);
            if (epochs > std::numeric_limits<std::size_t>::max()) {
                throw std::runtime_error("--epochs exceeds this build's range");
            }
            options.epochs = static_cast<std::size_t>(epochs);
        } else if (flag == "--learning-rate") {
            if (++index == argc) throw std::runtime_error("--learning-rate requires a positive number");
            options.learningRate = std::stod(argv[index]);
        } else {
            throw std::runtime_error("unknown training option: " + std::string(flag));
        }
    }
    if (!haveStore || options.epochs == 0 || !std::isfinite(options.learningRate) || options.learningRate <= 0.0) {
        throw std::runtime_error("training options are invalid");
    }
    return options;
}

} // namespace Felidae
