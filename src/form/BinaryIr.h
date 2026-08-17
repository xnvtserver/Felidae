#pragma once

#include "IrModule.h"

#include <filesystem>

namespace Felidae {

inline constexpr const char kBinaryIrExtension[] = ".fir";
inline constexpr std::uint32_t kBinaryIrVersion = 7;

// Both functions verify all code blocks and reject legacy execution opcodes.
// Version 5 starts with a fixed little-endian header (magic, version, entry
// procedure, and offset/count pairs for metadata, constants, text, symbols,
// programs, source maps, and code). It contains no pointers, AST nodes, or
// runtime values. Text is encoded as SentencePiece IDs and symbols are IDs only.
void writeBinaryIr(const std::filesystem::path& path, const IrModule& module);
IrModule loadBinaryIr(const std::filesystem::path& path);

} // namespace Felidae
