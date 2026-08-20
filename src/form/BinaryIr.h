#pragma once

#include "IrModule.h"

#include <filesystem>

namespace Felidae {

inline constexpr const char kBinaryIrExtension[] = ".bin";
inline constexpr std::uint32_t kBinaryIrVersion = 8;

// FELBIN v8 is an intentionally incompatible successor to the old FELIR/.fir
// container. Both functions verify all code blocks and reject legacy execution
// opcodes. The fixed little-endian header contains magic, version, entry
// procedure, and offset/count pairs for metadata, constants, text, symbols,
// programs, source maps, and code). It contains no pointers, AST nodes, or
// runtime values. Text is encoded as SentencePiece IDs and symbols are IDs only.
void writeBinaryIr(const std::filesystem::path& path, const IrModule& module);
IrModule loadBinaryIr(const std::filesystem::path& path);

} // namespace Felidae
