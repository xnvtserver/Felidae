#pragma once

#include "FelidaeIsa.h"

#include <cstdint>
#include <filesystem>

namespace Felidae {

inline constexpr char kFelidaeBinaryExtension[] = ".bin";
inline constexpr std::uint32_t kFelidaeBinaryVersion = 10;

void writeBinaryIsa(const std::filesystem::path& path, const IsaModule& module);
IsaModule loadBinaryIsa(const std::filesystem::path& path);
bool containsRuntimeSemanticOperation(const IsaModule& module);
VmDisplayContext makeIsaDisplayContext(const IsaModule& module);

} // namespace Felidae
