#pragma once

#include "IrModule.h"

#include <filesystem>

namespace Felidae {

inline constexpr const char kBinaryIrExtension[] = ".bin";
inline constexpr std::uint32_t kBinaryIrVersion = 12;

// The writer consumes a module already verified by the compiler boundary.
// The loader parses hostile bytes, checks the exact SentencePiece identity,
// verifies once, and returns the executable module.
void writeBinaryIr(const std::filesystem::path& path, const VerifiedIrModule& module);
VerifiedIrModule loadBinaryIr(const std::filesystem::path& path,
                              const std::string& expectedSentencePieceModelHash);
bool containsRuntimeSemanticOperation(const VerifiedIrModule& module);
VmDisplayContext makeIrDisplayContext(const VerifiedIrModule& module,
                                      VmTextDecoder decoder);

} // namespace Felidae
