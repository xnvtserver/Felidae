#include "BinaryIr.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <memory>
#include <type_traits>

namespace Felidae {
namespace {

constexpr std::array<char, 8> kMagic{'F','E','L','B','I','R','\0','\0'};
constexpr std::uint32_t kEndian = 0x01020304u;
constexpr std::uint32_t kMaximumItems = 1u << 24;
constexpr std::uint64_t kMaximumBytes = 256ull * 1024ull * 1024ull;
constexpr std::size_t kMinimumHeaderBytes = 8 + 4 + 4 + 4 + 4 + 4 + 4 + 4;

template <class T> void writeLe(std::ostream& out, T value) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    const auto raw = static_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        out.put(static_cast<char>((raw >> (index * 8u)) & 0xffu));
    }
    if (!out) throw IrError("cannot write FELBIR");
}

template <class T> T readLe(std::istream& in) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    U raw = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        const auto byte = in.get();
        if (byte == EOF) throw IrError("FELBIR is truncated");
        raw |= static_cast<U>(static_cast<unsigned char>(byte)) << (index * 8u);
    }
    return static_cast<T>(raw);
}

std::uint32_t count(std::size_t value, const char* name) {
    if (value > kMaximumItems) throw IrError(std::string("FELBIR ") + name + " exceeds its limit");
    return static_cast<std::uint32_t>(value);
}

void writePieces(std::ostream& out, std::span<const PieceId> pieces) {
    writeLe<std::uint32_t>(out, count(pieces.size(), "piece count"));
    for (const auto piece : pieces) writeLe<PieceId>(out, piece);
}

PieceSequence readPieces(std::istream& in, bool requireNonempty) {
    const auto size = readLe<std::uint32_t>(in);
    if ((requireNonempty && size == 0) || size > kMaximumItems) {
        throw IrError("FELBIR PieceId sequence is invalid");
    }
    PieceSequence pieces;
    pieces.reserve(size);
    for (std::uint32_t index = 0; index < size; ++index) pieces.push_back(readLe<PieceId>(in));
    return pieces;
}

void writeSpan(std::ostream& out, const IrSourceMapEntry::Span& span) {
    writeLe<std::int32_t>(out, span.startLine); writeLe<std::int32_t>(out, span.startColumn);
    writeLe<std::int32_t>(out, span.endLine); writeLe<std::int32_t>(out, span.endColumn);
}

IrSourceMapEntry::Span readSpan(std::istream& in) {
    return {readLe<std::int32_t>(in), readLe<std::int32_t>(in),
            readLe<std::int32_t>(in), readLe<std::int32_t>(in)};
}

void writeSymbols(std::ostream& out, std::span<const IrSymbolRef> symbols) {
    writeLe<std::uint32_t>(out, count(symbols.size(), "symbol count"));
    for (const auto symbol : symbols) writeLe<std::uint32_t>(out, static_cast<std::uint32_t>(symbol));
}

std::vector<IrSymbolRef> readSymbols(std::istream& in) {
    const auto size = readLe<std::uint32_t>(in);
    if (size > kMaximumItems) throw IrError("FELBIR symbol count exceeds its limit");
    std::vector<IrSymbolRef> symbols;
    symbols.reserve(size);
    for (std::uint32_t index = 0; index < size; ++index) symbols.push_back(readLe<std::uint32_t>(in));
    return symbols;
}

void writeProgram(std::ostream& out, const FelidaeIr& program) {
    writeLe<std::uint32_t>(out, count(program.registerCount, "register count"));
    writeLe<std::uint32_t>(out, count(program.words.size(), "code word count"));
    writeLe<std::uint32_t>(out, count(program.constants.size(), "constant count"));
    writeLe<std::uint32_t>(out, count(program.texts.size(), "text count"));
    writeLe<std::uint32_t>(out, count(program.symbols.size(), "symbol count"));
    writeLe<std::uint32_t>(out, count(program.programs.size(), "program count"));
    writeLe<std::uint32_t>(out, count(program.sourceMap.size(), "source-map count"));
    for (const auto word : program.words) writeLe<IrWord>(out, word);
    for (std::size_t index = 0; index < program.constants.size(); ++index) {
        const auto kind = program.constantKinds.empty() ? IrConstantKind::Number : program.constantKinds.at(index);
        writeLe<std::uint8_t>(out, static_cast<std::uint8_t>(kind));
        writeLe<IrConstant>(out, program.constants[index]);
    }
    for (const auto& text : program.texts) writePieces(out, text);
    for (const auto symbol : program.symbols) writeLe<std::uint32_t>(out, static_cast<std::uint32_t>(symbol));
    for (const auto child : program.programs) writeLe<IrWord>(out, child);
    for (const auto& entry : program.sourceMap) {
        writeLe<std::uint32_t>(out, count(entry.instructionWord, "source-map offset"));
        writeSpan(out, entry.sourceSpan);
    }
}

FelidaeIr readProgram(std::istream& in) {
    FelidaeIr program;
    program.registerCount = readLe<std::uint32_t>(in);
    const auto codeCount = readLe<std::uint32_t>(in);
    const auto constantCount = readLe<std::uint32_t>(in);
    const auto textCount = readLe<std::uint32_t>(in);
    const auto symbolCount = readLe<std::uint32_t>(in);
    const auto programCount = readLe<std::uint32_t>(in);
    const auto sourceCount = readLe<std::uint32_t>(in);
    if (codeCount > kMaximumItems || constantCount > kMaximumItems || textCount > kMaximumItems ||
        symbolCount > kMaximumItems || programCount > kMaximumItems || sourceCount > kMaximumItems) {
        throw IrError("FELBIR program table exceeds its limit");
    }
    program.words.reserve(codeCount);
    for (std::uint32_t index = 0; index < codeCount; ++index) program.words.push_back(readLe<IrWord>(in));
    program.constants.reserve(constantCount); program.constantKinds.reserve(constantCount);
    for (std::uint32_t index = 0; index < constantCount; ++index) {
        const auto rawKind = readLe<std::uint8_t>(in);
        if (rawKind > static_cast<std::uint8_t>(IrConstantKind::Text)) throw IrError("FELBIR constant kind is invalid");
        program.constantKinds.push_back(static_cast<IrConstantKind>(rawKind));
        program.constants.push_back(readLe<IrConstant>(in));
    }
    program.texts.reserve(textCount);
    for (std::uint32_t index = 0; index < textCount; ++index) program.texts.push_back(readPieces(in, false));
    program.symbols.reserve(symbolCount);
    for (std::uint32_t index = 0; index < symbolCount; ++index) program.symbols.push_back(readLe<std::uint32_t>(in));
    program.programs.reserve(programCount);
    for (std::uint32_t index = 0; index < programCount; ++index) program.programs.push_back(readLe<IrWord>(in));
    program.sourceMap.reserve(sourceCount);
    for (std::uint32_t index = 0; index < sourceCount; ++index) program.sourceMap.push_back({readLe<std::uint32_t>(in), readSpan(in)});
    return program;
}

} // namespace

void writeBinaryIr(const std::filesystem::path& path, const VerifiedIrModule& verified) {
    const auto& module = verified.get();
    if (module.symbolTable.empty() || module.sentencePieceModelHash.empty()) {
        throw IrError("FELBIR writer requires canonical PieceId module metadata");
    }
    const auto temporary = path.string() + ".tmp." + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw IrError("cannot create temporary FELBIR");
        out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
        writeLe<std::uint32_t>(out, kBinaryIrVersion); writeLe<std::uint32_t>(out, kEndian);
        writeLe<std::uint32_t>(out, count(module.sentencePieceModelHash.size(), "model identity size"));
        out.write(module.sentencePieceModelHash.data(), static_cast<std::streamsize>(module.sentencePieceModelHash.size()));
        writeLe<std::uint32_t>(out, static_cast<std::uint32_t>(module.entryProcedure));
        writeLe<std::uint32_t>(out, count(module.procedures.size(), "procedure count"));
        writeLe<std::uint32_t>(out, count(module.factTypes.size(), "fact-type count"));
        writeLe<std::uint32_t>(out, count(module.symbolTable.size(), "symbol table count"));
        writeProgram(out, module.ir);
        std::vector<IrSymbolRef> procedures;
        procedures.reserve(module.procedures.size());
        for (const auto& [symbol, _] : module.procedures) procedures.push_back(symbol);
        std::sort(procedures.begin(), procedures.end());
        for (const auto symbol : procedures) {
            const auto& procedure = module.procedures.at(symbol);
            writeLe<std::uint32_t>(out, static_cast<std::uint32_t>(symbol));
            writeSymbols(out, procedure.positionalParameters); writeSymbols(out, procedure.namedParameters);
            writeSpan(out, procedure.sourceSpan); writeProgram(out, procedure.ir);
        }
        for (const auto& type : module.factTypes) {
            writeLe<std::uint32_t>(out, static_cast<std::uint32_t>(type.symbol));
            writeSymbols(out, type.parents); writeSpan(out, type.sourceSpan);
        }
        for (const auto& pieces : module.symbolTable) writePieces(out, pieces);
        out.close();
        if (!out) throw IrError("cannot write FELBIR");
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error) throw IrError("cannot replace FELBIR: " + error.message());
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

VerifiedIrModule loadBinaryIr(const std::filesystem::path& path,
                              const std::string& expectedSentencePieceModelHash) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw IrError("cannot open FELBIR: " + path.string());
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) < kMinimumHeaderBytes) throw IrError("FELBIR is truncated");
    if (static_cast<std::uint64_t>(end) > kMaximumBytes) throw IrError("FELBIR exceeds its byte limit");
    in.seekg(0);
    std::array<char, 8> magic{}; in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (magic != kMagic) throw IrError("FELBIR magic is invalid");
    if (readLe<std::uint32_t>(in) != kBinaryIrVersion) throw IrError("FELBIR version is unsupported");
    if (readLe<std::uint32_t>(in) != kEndian) throw IrError("FELBIR endian marker is invalid");
    const auto hashSize = readLe<std::uint32_t>(in);
    if (hashSize == 0 || hashSize > 256) throw IrError("FELBIR model identity is invalid");
    std::string modelHash(hashSize, '\0'); in.read(modelHash.data(), hashSize);
    if (!in) throw IrError("FELBIR model identity is truncated");
    if (modelHash != expectedSentencePieceModelHash) throw IrError("FELBIR SentencePiece model identity does not match this VM");
    IrModule module; module.sentencePieceModelHash = std::move(modelHash);
    module.entryProcedure = readLe<std::uint32_t>(in);
    const auto procedureCount = readLe<std::uint32_t>(in);
    const auto factCount = readLe<std::uint32_t>(in);
    const auto symbolCount = readLe<std::uint32_t>(in);
    if (procedureCount == 0 || procedureCount > kMaximumItems || factCount > kMaximumItems ||
        symbolCount == 0 || symbolCount > kMaximumItems) throw IrError("FELBIR module count is invalid");
    module.ir = readProgram(in);
    for (std::uint32_t index = 0; index < procedureCount; ++index) {
        const auto symbol = readLe<std::uint32_t>(in);
        IrProcedure procedure; procedure.positionalParameters = readSymbols(in);
        procedure.namedParameters = readSymbols(in); procedure.sourceSpan = readSpan(in);
        procedure.ir = readProgram(in);
        if (!module.procedures.emplace(symbol, std::move(procedure)).second) throw IrError("FELBIR procedure is duplicated");
    }
    module.factTypes.reserve(factCount);
    for (std::uint32_t index = 0; index < factCount; ++index) {
        IrFactType type; type.symbol = readLe<std::uint32_t>(in);
        type.parents = readSymbols(in); type.sourceSpan = readSpan(in);
        module.factTypes.push_back(std::move(type));
    }
    module.symbolTable.reserve(symbolCount);
    for (std::uint32_t index = 0; index < symbolCount; ++index) module.symbolTable.push_back(readPieces(in, true));
    if (in.peek() != EOF) throw IrError("FELBIR has trailing data");
    return verifyIrModule(std::move(module));
}

bool containsRuntimeSemanticOperation(const VerifiedIrModule& verified) {
    const auto& module = verified.get();
    const auto contains = [](const FelidaeIr& program) {
        for (std::size_t pc = 0; pc < program.words.size(); pc += compilerInstructionWidth(program, pc)) {
            if (static_cast<IrOpcode>(program.words[pc]) == IrOpcode::SemanticEval) return true;
        }
        return false;
    };
    if (contains(module.ir)) return true;
    for (const auto& [_, procedure] : module.procedures) if (contains(procedure.ir)) return true;
    return false;
}

VmDisplayContext makeIrDisplayContext(const VerifiedIrModule& verified, VmTextDecoder decoder) {
    (void)verified;
    VmDisplayContext context;
    context.textDecoder = decoder;
    context.symbolDecoder = [decoder = std::move(decoder)](IrSymbolRef symbol) {
        const auto pieces = runtimeSymbolPieces(symbol);
        return !decoder || pieces.empty() ? std::string{} : decoder(pieces);
    };
    return context;
}

} // namespace Felidae
