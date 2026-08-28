#pragma once

#include "IrModule.h"
#include "../Version.h"

#include <filesystem>

namespace Felidae {

inline constexpr const char kBinaryIrExtension[] = ".bin";
// FELBIR embeds and checks LANGUAGE_VERSION (Version.h) directly as its
// compatibility marker. There is no separate binary-format version number to
// keep in sync -- Version.h is the one source of truth for every version
// this project reports, including this one.

// The writer consumes a module already verified by the compiler boundary.
// The loader parses hostile bytes, checks the exact SentencePiece identity,
// verifies once, and returns the executable module.
void writeBinaryIr(const std::filesystem::path& path, const VerifiedIrModule& module);
VerifiedIrModule loadBinaryIr(const std::filesystem::path& path,
                              const std::string& expectedSentencePieceModelIdentity);
bool containsRuntimeSemanticOperation(const VerifiedIrModule& module);
VmDisplayContext makeIrDisplayContext(const VerifiedIrModule& module,
                                      VmTextDecoder decoder);

} // namespace Felidae
