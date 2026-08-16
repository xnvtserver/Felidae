#include "CompilerFrontend.h"

#include "IntegerParser.h"
#include "IrCodeGenerator.h"
#include "SentencePieceModel.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace Felidae {
namespace {
constexpr std::uintmax_t kStreamingReadThresholdBytes = 10ull * 1024ull * 1024ull;
constexpr std::size_t kReadChunkBytes = 1024ull * 1024ull;
}

std::string readSourceFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file: " + path.string());
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (!ec && size <= kStreamingReadThresholdBytes) {
        std::string text(static_cast<std::size_t>(size), '\0');
        if (!text.empty()) {
            in.read(text.data(), static_cast<std::streamsize>(text.size()));
            if (!in && !in.eof()) throw std::runtime_error("Cannot read file: " + path.string());
        }
        return text;
    }
    std::string text;
    if (!ec) text.reserve(static_cast<std::size_t>(std::min<std::uintmax_t>(size, kStreamingReadThresholdBytes)));
    std::vector<char> buffer(kReadChunkBytes);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = in.gcount();
        if (read > 0) text.append(buffer.data(), static_cast<std::size_t>(read));
    }
    if (!in.eof()) throw std::runtime_error("Cannot read file: " + path.string());
    return text;
}

std::filesystem::path resolveProgramEntryPath(const fs::path& path) {
    fs::path normalized = fs::absolute(path).lexically_normal();
    std::error_code ec;
    if (!fs::is_directory(normalized, ec)) return normalized;
    const fs::path mainFile = normalized / "main.fx";
    if (fs::exists(mainFile, ec) && fs::is_regular_file(mainFile, ec)) return mainFile.lexically_normal();
    throw std::runtime_error("Project directory does not contain main.fx: " + normalized.string());
}

Program parseProgramText(std::string text) {
    IntegerTokenList input(felidaeSentencePieceModel(), std::move(text));
    return IntegerParser(input).parseProgram();
}

Program parseProgramFile(const fs::path& path) {
    return parseProgramText(readSourceFile(resolveProgramEntryPath(path)));
}

IrModule compileProgramTextToIr(std::string text) {
    return IrCodeGenerator{}.compile(parseProgramText(std::move(text)));
}

IrModule compileProgramFileToIr(const fs::path& path) {
    return compileProgramTextToIr(readSourceFile(resolveProgramEntryPath(path)));
}

std::optional<FelidaeIr> tryCompileExpressionTextToIr(const std::string& text) {
    try {
        IntegerTokenList input(felidaeSentencePieceModel(), text);
        return IntegerParser(input).compileExpressionIr();
    } catch (const IntegerParserError&) {
        return std::nullopt;
    }
}

} // namespace Felidae
