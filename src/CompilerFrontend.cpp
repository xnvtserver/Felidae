#include "CompilerFrontend.h"

#include "IntegerParser.h"
#include "IrCodeGenerator.h"
#include "SentencePieceModel.h"
#include "Symbol.h"

#include <sentencepiece_processor.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>
#include <unordered_map>

namespace fs = std::filesystem;

namespace Felidae {
namespace {
constexpr std::uintmax_t kStreamingReadThresholdBytes = 10ull * 1024ull * 1024ull;
constexpr std::size_t kReadChunkBytes = 1024ull * 1024ull;

std::string normalizeSource(std::string text) {
    // A UTF-8 BOM is transport metadata, not Felidae syntax. Retaining it
    // creates an unexpected first SentencePiece token before valid syntax.
    constexpr char kUtf8Bom[] = {'\xEF', '\xBB', '\xBF'};
    if (text.size() >= std::size(kUtf8Bom) &&
        std::equal(std::begin(kUtf8Bom), std::end(kUtf8Bom), text.begin())) {
        text.erase(0, std::size(kUtf8Bom));
    }
    return text;
}
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
        return normalizeSource(std::move(text));
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
    return normalizeSource(std::move(text));
}

std::filesystem::path resolveProgramEntryPath(const fs::path& path) {
    fs::path normalized = fs::absolute(path).lexically_normal();
    std::error_code ec;
    if (!fs::is_directory(normalized, ec)) return normalized;
    const fs::path mainFile = normalized / "main.fx";
    if (fs::exists(mainFile, ec) && fs::is_regular_file(mainFile, ec)) return mainFile.lexically_normal();
    throw std::runtime_error("Project directory does not contain main.fx: " + normalized.string());
}

Program parseProgramText(std::string text, const CompilerOptions& options) {
    IntegerTokenList input(felidaeSentencePieceModel(), std::move(text));
    // Mixfix declarations and uses share one parser-owned registry while each
    // physical source line is independently SentencePiece-encoded.
    return IntegerParser(input, std::make_shared<OperatorRegistry>(), options.mixfixModel).parseProgram();
}

Program parseProgramFile(const fs::path& path, const CompilerOptions& options) {
    return parseProgramText(readSourceFile(resolveProgramEntryPath(path)), options);
}

IrModule compileProgramTextToIr(std::string text, const CompilerOptions& options) {
    auto module = IrCodeGenerator{}.compile(parseProgramText(std::move(text), options));
    std::vector<IrSymbolRef> symbols;
    const auto collectProgram = [&](const FelidaeIr& program) {
        symbols.insert(symbols.end(), program.symbols.begin(), program.symbols.end());
    };
    collectProgram(module.ir);
    symbols.push_back(module.entryProcedure);
    for (const auto& [symbol, procedure] : module.procedures) {
        symbols.push_back(symbol);
        collectProgram(procedure.ir);
        symbols.insert(symbols.end(), procedure.positionalParameters.begin(), procedure.positionalParameters.end());
        symbols.insert(symbols.end(), procedure.namedParameters.begin(), procedure.namedParameters.end());
    }
    for (const auto& type : module.factTypes) {
        symbols.push_back(type.symbol);
        symbols.insert(symbols.end(), type.parents.begin(), type.parents.end());
    }
    std::sort(symbols.begin(), symbols.end());
    symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
    std::unordered_map<IrSymbolRef, IrSymbolRef> indexes;
    indexes.reserve(symbols.size());
    module.symbolTable.reserve(symbols.size());
    const auto& model = felidaeSentencePieceModel();
    for (const auto symbol : symbols) {
        const auto spelling = symbolNameForId(symbol);
        if (symbol == 0 || spelling.empty()) throw IntegerParserError("IR symbol has no canonical lexical spelling");
        std::vector<int> encoded;
        const auto status = model.Encode(spelling, &encoded);
        if (!status.ok() || encoded.empty()) throw IntegerParserError("IR symbol cannot be encoded by the fixed SentencePiece model");
        PieceSequence pieces;
        pieces.reserve(encoded.size());
        for (const auto piece : encoded) {
            if (piece < 0) throw IntegerParserError("SentencePiece emitted an invalid symbol piece");
            pieces.push_back(static_cast<PieceId>(piece));
        }
        const auto index = static_cast<IrSymbolRef>(module.symbolTable.size() + 1);
        indexes.emplace(symbol, index);
        module.symbolTable.push_back(std::move(pieces));
    }
    const auto remap = [&](IrSymbolRef& symbol) { symbol = indexes.at(symbol); };
    const auto remapProgram = [&](FelidaeIr& program) {
        for (auto& symbol : program.symbols) remap(symbol);
    };
    remapProgram(module.ir);
    remap(module.entryProcedure);
    std::unordered_map<IrSymbolRef, IrProcedure> procedures;
    procedures.reserve(module.procedures.size());
    for (auto& [symbol, procedure] : module.procedures) {
        remapProgram(procedure.ir);
        for (auto& parameter : procedure.positionalParameters) remap(parameter);
        for (auto& parameter : procedure.namedParameters) remap(parameter);
        procedures.emplace(indexes.at(symbol), std::move(procedure));
    }
    module.procedures = std::move(procedures);
    for (auto& type : module.factTypes) {
        remap(type.symbol);
        for (auto& parent : type.parents) remap(parent);
    }
    module.sentencePieceModelHash = felidaeSentencePieceModelIdentity();
    return module;
}

IrModule compileProgramFileToIr(const fs::path& path, const CompilerOptions& options) {
    return compileProgramTextToIr(readSourceFile(resolveProgramEntryPath(path)), options);
}

std::optional<FelidaeIr> tryCompileExpressionTextToIr(const std::string& text) {
    try {
        IntegerTokenList input(felidaeSentencePieceModel(), text);
        return IntegerParser(input, std::make_shared<OperatorRegistry>()).compileExpressionIr();
    } catch (const IntegerParserError&) {
        return std::nullopt;
    }
}

} // namespace Felidae
