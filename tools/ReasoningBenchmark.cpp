#include "CompilerFrontend.h"
#include "SentencePieceModel.h"
#include "form/FelidaeIsa.h"
#include "form/IsaLowerer.h"
#include "form/RegisterVm.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::string jsonEscape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 16);
    for (const char character : value) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += character; break;
        }
    }
    return escaped;
}

std::int64_t micros(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
}

std::int64_t percentile(std::vector<std::int64_t> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    return values[index];
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::size_t iterations = 1;
        std::filesystem::path source;
        std::vector<std::string> expected;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--iterations") {
                if (++index == argc) throw std::runtime_error("--iterations requires a value");
                const auto parsed = std::stoull(argv[index]);
                if (parsed == 0 || parsed > 10000) {
                    throw std::runtime_error("iterations must be in [1, 10000]");
                }
                iterations = static_cast<std::size_t>(parsed);
            } else if (argument == "--expect") {
                if (++index == argc) throw std::runtime_error("--expect requires display text");
                expected.emplace_back(argv[index]);
            } else if (source.empty()) {
                source = argv[index];
            } else {
                throw std::runtime_error("unexpected argument: " + std::string(argument));
            }
        }
        if (source.empty()) {
            std::cerr << "usage: felidae_reasoning_benchmark [--iterations N]"
                         " <source.fx> [--expect text]...\n";
            return 2;
        }

        const auto compileStarted = Clock::now();
        const auto ir = Felidae::compileProgramFileToIr(source);
        Felidae::verifyIrModule(ir);
        const auto isa = Felidae::IsaLowerer::lowerModule(ir);
        Felidae::verifyIsaModule(isa);
        const auto compileFinished = Clock::now();

        std::vector<std::int64_t> executionMicros;
        executionMicros.reserve(iterations);
        std::string baseline;
        bool deterministic = true;
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            Felidae::FelidaeKnowledgeRuntime runtime;
            runtime.installIsaModule(isa);
            const auto started = Clock::now();
            const auto result = Felidae::RegisterVm{}.executeIsaMain(isa, runtime);
            const auto finished = Clock::now();
            executionMicros.push_back(micros(started, finished));
            const auto rendered = Felidae::vmValueToDisplayString(result);
            if (iteration == 0) baseline = rendered;
            else deterministic = deterministic && rendered == baseline;
        }

        bool expectationsPassed = true;
        for (const auto& needle : expected) {
            expectationsPassed = expectationsPassed && baseline.find(needle) != std::string::npos;
        }
        const auto total = std::accumulate(
            executionMicros.begin(), executionMicros.end(), std::int64_t{0});
        const auto mean = total / static_cast<std::int64_t>(executionMicros.size());
        std::cout << "{\"schema_version\":1"
                  << ",\"source\":\"" << jsonEscape(source.generic_string()) << "\""
                  << ",\"iterations\":" << iterations
                  << ",\"compile_micros\":" << micros(compileStarted, compileFinished)
                  << ",\"vm_mean_micros\":" << mean
                  << ",\"vm_median_micros\":" << percentile(executionMicros, 0.50)
                  << ",\"vm_p95_micros\":" << percentile(executionMicros, 0.95)
                  << ",\"deterministic\":" << (deterministic ? "1.0" : "0.0")
                  << ",\"expectations_passed\":" << (expectationsPassed ? "1.0" : "0.0")
                  << ",\"result\":\"" << jsonEscape(baseline) << "\"}\n";
        return deterministic && expectationsPassed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "reasoning benchmark error: " << error.what() << '\n';
        return 1;
    }
}
