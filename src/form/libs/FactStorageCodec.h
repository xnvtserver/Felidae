#pragma once

#include "form/RegisterVm.h"

#include <functional>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Felidae::Form {

struct StoredFactRecord {
  VmFactPtr fact;
  std::optional<std::string> source;
};

struct StoredFactTypeRecord {
  IrSymbolRef type = 0;
  std::vector<IrSymbolRef> parents;
  std::vector<std::vector<IrSymbolRef>> indexes;
};

using StoredSymbolInterner = std::function<IrSymbolRef(PieceSequence)>;
using StoredValueKey = std::vector<std::uint64_t>;

StoredValueKey encodeStoredValueKey(
    const VmValue &value, std::span<const PieceSequence> symbols);

std::string encodeStoredFact(const VmFact &fact,
                             std::optional<std::string_view> source,
                             std::span<const PieceSequence> symbols);
StoredFactRecord decodeStoredFact(std::string_view bytes,
                                  const StoredSymbolInterner &intern);
std::string encodeStoredFactType(
    IrSymbolRef type, std::span<const IrSymbolRef> parents,
    std::span<const std::vector<IrSymbolRef>> indexes,
    std::span<const PieceSequence> symbols);
StoredFactTypeRecord decodeStoredFactType(
    std::string_view bytes, const StoredSymbolInterner &intern);

} // namespace Felidae::Form
