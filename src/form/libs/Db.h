#pragma once

#include "form/RegisterVm.h"

#include <filesystem>

namespace Felidae::Form::Db {

// Internal automatic-persistence helper. Rewrites one database from immutable
// VM fact snapshots; it has no source-language builtin.
void sync(const std::filesystem::path &path,
          std::span<const VmFactPtr> facts,
          std::span<const PieceSequence> symbolTable,
          const VmTextDecoder &decodeText,
          const VmFactPtr &emptySchema = {});

} // namespace Felidae::Form::Db
