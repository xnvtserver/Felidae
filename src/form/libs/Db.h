#pragma once

#include "form/RegisterVm.h"

#include <filesystem>

namespace Felidae::Form::Db {

// Rewrites one fact-only Felidae database from the VM's immutable fact
// snapshots. Source parsing and compiler services are intentionally absent:
// db.sync is a DML persistence boundary owned entirely by RegisterVm.
void sync(const std::filesystem::path &path,
          std::span<const VmFactPtr> facts,
          std::span<const PieceSequence> symbolTable,
          const VmTextDecoder &decodeText);

} // namespace Felidae::Form::Db
