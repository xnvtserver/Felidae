#include "RegisterVm.h"

#include "IrModule.h"
#include "SemanticOperation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace Felidae {

RuntimeValueKind runtimeValueKind(const VmValue &value) noexcept {
  if (std::holds_alternative<VmNil>(value))
    return RuntimeValueKind::Nil;
  if (std::holds_alternative<double>(value))
    return RuntimeValueKind::Number;
  if (std::holds_alternative<VmDegree>(value))
    return RuntimeValueKind::Degree;
  if (std::holds_alternative<VmText>(value))
    return RuntimeValueKind::Text;
  if (std::holds_alternative<VmSymbol>(value))
    return RuntimeValueKind::Symbol;
  if (std::holds_alternative<VmArrayPtr>(value))
    return RuntimeValueKind::Array;
  if (std::holds_alternative<VmMapPtr>(value))
    return RuntimeValueKind::Map;
  return RuntimeValueKind::Fact;
}

std::size_t irInstructionWidth(const FelidaeIr &ir, std::size_t pc) {
  if (pc >= ir.words.size() || ir.words[pc] >= kIrOpcodeCount) {
    throw IrError("IR contains an invalid opcode");
  }
  const auto bounded = [&](std::size_t width) {
    if (width > ir.words.size() - pc)
      throw IrError("IR instruction is truncated");
    return width;
  };
  const auto opcode = static_cast<IrOpcode>(ir.words[pc]);
  switch (opcode) {
  case IrOpcode::End:
    return bounded(1);
  case IrOpcode::Jump:
    return bounded(2);
  case IrOpcode::LoadConst:
  case IrOpcode::LoadSymbol:
  case IrOpcode::StoreSymbol:
  case IrOpcode::Move:
  case IrOpcode::JumpIfFalse:
  case IrOpcode::CallNative:
  case IrOpcode::MakeFact:
  case IrOpcode::Return:
    return bounded(3);
  case IrOpcode::ForEachFact:
  case IrOpcode::Add:
  case IrOpcode::Sub:
  case IrOpcode::Mul:
  case IrOpcode::Div:
  case IrOpcode::Mod:
  case IrOpcode::GetField:
  case IrOpcode::SetField:
  case IrOpcode::Similarity:
  case IrOpcode::HierarchyIsA:
  case IrOpcode::HierarchyCommonAncestors:
  case IrOpcode::HierarchyLeastCommonAncestors:
  case IrOpcode::HierarchyMostGeneralAncestors:
  case IrOpcode::TemporalRank:
    return bounded(4);
  case IrOpcode::Compare:
    return bounded(5);
  case IrOpcode::Membership:
    return bounded(6);
  case IrOpcode::Call:
  case IrOpcode::SemanticEval:
  case IrOpcode::Numeric:
  case IrOpcode::MakeArray:
  case IrOpcode::CallNamed:
  case IrOpcode::MakeMap: {
    bounded(4);
    const auto stride =
        opcode == IrOpcode::CallNamed || opcode == IrOpcode::MakeMap ? 2u : 1u;
    const auto count = ir.words[pc + 3];
    if (count > (ir.words.size() - pc - 4) / stride) {
      throw IrError("IR dynamic instruction is truncated");
    }
    return 4 + static_cast<std::size_t>(count) * stride;
  }
  case IrOpcode::Count:
    break;
  }
  throw IrError("IR contains an invalid opcode");
}

VmDegree::VmDegree(double value) : value(value) {
  if (!std::isfinite(value) || value < 0.0 || value > 1.0)
    throw IrError("Degree must be finite and within [0,1]");
}
namespace {

#ifndef NDEBUG
bool vmTraceEnabled() {
  static const bool enabled = [] {
    const auto *value = std::getenv("FELIDAE_TRACE");
    return value != nullptr && *value != '\0' && std::string_view(value) != "0";
  }();
  return enabled;
}
#endif

constexpr std::size_t kMaximumIrWords = 1'000'000;
constexpr std::size_t kMaximumRegisters = 65'536;
constexpr std::size_t kMaximumIrTableEntries = 1'000'000;
constexpr std::size_t kMaximumTextBytes = 16ull * 1024ull * 1024ull;

IrOpcode opcodeAt(const FelidaeIr &ir, std::size_t index) {
  if (ir.words[index] >= kIrOpcodeCount) {
    throw IrError("IR contains an invalid opcode");
  }
  return static_cast<IrOpcode>(ir.words[index]);
}

void requireRegister(const FelidaeIr &ir, IrWord word) {
  if (word >= ir.registerCount)
    throw IrError("IR references an invalid register");
}

void requireInitialized(const std::vector<bool> &initialized, IrWord word) {
  if (!initialized[word])
    throw IrError("IR reads an uninitialized register");
}

void requireFlowRead(const std::vector<bool> &initialized, IrWord registerId) {
  if (!initialized[registerId])
    throw IrError("IR control-flow path reads an uninitialized register");
}
constexpr std::size_t kMaximumVmValueDepth = 256;

bool vmValuesEqualAtDepth(const VmValue &left, const VmValue &right,
                          std::size_t depth) {
  if (depth > kMaximumVmValueDepth) {
    throw IrError("VM value comparison exceeds its nesting limit");
  }
  if (left.index() != right.index())
    return false;
  if (std::holds_alternative<VmNil>(left))
    return true;
  if (const auto *value = std::get_if<double>(&left))
    return *value == std::get<double>(right);
  if (const auto *value = std::get_if<VmDegree>(&left))
    return *value == std::get<VmDegree>(right);
  if (const auto *value = std::get_if<VmText>(&left))
    return *value == std::get<VmText>(right);
  if (const auto *value = std::get_if<VmSymbol>(&left))
    return *value == std::get<VmSymbol>(right);
  if (const auto *leftArray = std::get_if<VmArrayPtr>(&left)) {
    const auto &rightArray = std::get<VmArrayPtr>(right);
    if (!*leftArray || !rightArray)
      return *leftArray == rightArray;
    if ((*leftArray)->values.size() != rightArray->values.size())
      return false;
    for (std::size_t index = 0; index < (*leftArray)->values.size(); ++index) {
      if (!vmValuesEqualAtDepth((*leftArray)->values[index],
                                rightArray->values[index], depth + 1))
        return false;
    }
    return true;
  }
  if (const auto *leftMap = std::get_if<VmMapPtr>(&left)) {
    const auto &rightMap = std::get<VmMapPtr>(right);
    if (!*leftMap || !rightMap)
      return *leftMap == rightMap;
    if ((*leftMap)->entries.size() != rightMap->entries.size())
      return false;
    for (std::size_t index = 0; index < (*leftMap)->entries.size(); ++index) {
      const auto &[leftKey, leftValue] = (*leftMap)->entries[index];
      const auto &[rightKey, rightValue] = rightMap->entries[index];
      if (leftKey != rightKey ||
          !vmValuesEqualAtDepth(leftValue, rightValue, depth + 1))
        return false;
    }
    return true;
  }
  if (const auto *leftFact = std::get_if<VmFactPtr>(&left)) {
    const auto &rightFact = std::get<VmFactPtr>(right);
    if (!*leftFact || !rightFact)
      return *leftFact == rightFact;
    if ((*leftFact)->type != rightFact->type ||
        (*leftFact)->fields.size() != rightFact->fields.size())
      return false;
    for (std::size_t index = 0; index < (*leftFact)->fields.size(); ++index) {
      const auto &[leftKey, leftValue] = (*leftFact)->fields[index];
      const auto &[rightKey, rightValue] = rightFact->fields[index];
      if (leftKey != rightKey ||
          !vmValuesEqualAtDepth(leftValue, rightValue, depth + 1))
        return false;
    }
    return true;
  }
  throw IrError("VM equality received an unsupported value variant");
}

bool vmValuesEqual(const VmValue &left, const VmValue &right) {
  return vmValuesEqualAtDepth(left, right, 0);
}

double evaluateNumericOperation(NumericOperation operation,
                                std::span<const double> operands) {
  if (operation >= NumericOperation::Count ||
      operands.size() != numericOperationArity(operation)) {
    throw IrError("numeric operation has an invalid operation ID or arity");
  }
  if (operation == NumericOperation::IsFinite)
    return std::isfinite(operands[0]) ? 1.0 : 0.0;
  if (operation == NumericOperation::IsNan)
    return std::isnan(operands[0]) ? 1.0 : 0.0;
  if (!std::all_of(operands.begin(), operands.end(),
                   [](double value) { return std::isfinite(value); })) {
    throw IrError("numeric operation requires finite operands");
  }

  double result = 0.0;
  switch (operation) {
  case NumericOperation::Min:
    result = std::min(operands[0], operands[1]);
    break;
  case NumericOperation::Max:
    result = std::max(operands[0], operands[1]);
    break;
  case NumericOperation::Abs:
    result = std::abs(operands[0]);
    break;
  case NumericOperation::Diff:
    result = std::abs(operands[0] - operands[1]);
    break;
  case NumericOperation::Average:
    result = std::midpoint(operands[0], operands[1]);
    break;
  case NumericOperation::WeightedAverage: {
    const long double totalWeight =
        static_cast<long double>(operands[2]) + operands[3];
    if (totalWeight == 0.0L)
      throw IrError("WEIGHTED_AVG total weight must not be zero");
    const long double weighted =
        static_cast<long double>(operands[2]) * operands[0] +
        static_cast<long double>(operands[3]) * operands[1];
    result = static_cast<double>(weighted / totalWeight);
    break;
  }
  case NumericOperation::Clamp:
    if (operands[1] > operands[2])
      throw IrError("CLAMP minimum must not exceed maximum");
    result = std::clamp(operands[0], operands[1], operands[2]);
    break;
  case NumericOperation::Floor:
    result = std::floor(operands[0]);
    break;
  case NumericOperation::Ceil:
    result = std::ceil(operands[0]);
    break;
  case NumericOperation::Round:
    result = std::round(operands[0]);
    break;
  case NumericOperation::Trunc:
    result = std::trunc(operands[0]);
    break;
  case NumericOperation::Sqrt:
    if (operands[0] < 0.0)
      throw IrError("SQRT operand must not be negative");
    result = std::sqrt(operands[0]);
    break;
  case NumericOperation::Cbrt:
    result = std::cbrt(operands[0]);
    break;
  case NumericOperation::Pow:
    result = std::pow(operands[0], operands[1]);
    break;
  case NumericOperation::Exp:
    result = std::exp(operands[0]);
    break;
  case NumericOperation::Log:
  case NumericOperation::Log10:
    if (operands[0] <= 0.0)
      throw IrError("LOG operand must be greater than zero");
    result = operation == NumericOperation::Log ? std::log(operands[0])
                                                : std::log10(operands[0]);
    break;
  case NumericOperation::Lerp:
    result = std::lerp(operands[0], operands[1], operands[2]);
    break;
  case NumericOperation::Sign:
    result = static_cast<double>((0.0 < operands[0]) - (operands[0] < 0.0));
    break;
  case NumericOperation::Reciprocal:
    if (operands[0] == 0.0)
      throw IrError("RECIPROCAL operand must not be zero");
    result = 1.0 / operands[0];
    break;
  case NumericOperation::Square:
    result = operands[0] * operands[0];
    break;
  case NumericOperation::Cube:
    result = operands[0] * operands[0] * operands[0];
    break;
  case NumericOperation::InRange:
    if (operands[1] > operands[2])
      throw IrError("IN_RANGE minimum must not exceed maximum");
    result =
        operands[0] >= operands[1] && operands[0] <= operands[2] ? 1.0 : 0.0;
    break;
  case NumericOperation::IsFinite:
  case NumericOperation::IsNan:
  case NumericOperation::Count:
    break;
  }
  if (!std::isfinite(result))
    throw IrError("numeric operation produced a non-finite result");
  return result;
}

double similarityDegreeAtDepth(const VmValue &left, const VmValue &right,
                               std::size_t depth) {
  if (depth > kMaximumVmValueDepth) {
    throw IrError("VM similarity exceeds its nesting limit");
  }
  if (const auto a = std::get_if<double>(&left)) {
    if (const auto b = std::get_if<double>(&right))
      return 1.0 / (1.0 + std::abs(*a - *b));
  }
  if (const auto a = std::get_if<VmDegree>(&left)) {
    if (const auto b = std::get_if<VmDegree>(&right))
      return 1.0 - std::abs(a->value - b->value);
  }
  if (const auto a = std::get_if<VmText>(&left)) {
    if (const auto b = std::get_if<VmText>(&right))
      return a->pieces == b->pieces ? 1.0 : 0.0;
  }
  if (const auto a = std::get_if<VmSymbol>(&left)) {
    if (const auto b = std::get_if<VmSymbol>(&right))
      return a->value == b->value ? 1.0 : 0.0;
  }
  if (const auto a = std::get_if<VmArrayPtr>(&left)) {
    if (const auto b = std::get_if<VmArrayPtr>(&right)) {
      if (!*a || !*b)
        return *a == *b ? 1.0 : 0.0;
      const auto limit = std::max((*a)->values.size(), (*b)->values.size());
      if (limit == 0)
        return 1.0;
      double sum = 0.0;
      for (std::size_t i = 0;
           i < std::min((*a)->values.size(), (*b)->values.size()); ++i)
        sum += similarityDegreeAtDepth((*a)->values[i], (*b)->values[i],
                                       depth + 1);
      return sum / static_cast<double>(limit);
    }
  }
  if (const auto a = std::get_if<VmMapPtr>(&left)) {
    if (const auto b = std::get_if<VmMapPtr>(&right)) {
      if (!*a || !*b)
        return *a == *b ? 1.0 : 0.0;
      const auto limit = std::max((*a)->entries.size(), (*b)->entries.size());
      if (limit == 0)
        return 1.0;
      double sum = 0.0;
      for (const auto &[key, value] : (*a)->entries)
        for (const auto &[otherKey, otherValue] : (*b)->entries)
          if (key == otherKey) {
            sum += similarityDegreeAtDepth(value, otherValue, depth + 1);
            break;
          }
      return sum / static_cast<double>(limit);
    }
  }
  if (const auto a = std::get_if<VmFactPtr>(&left)) {
    if (const auto b = std::get_if<VmFactPtr>(&right)) {
      if (!*a || !*b)
        return *a == *b ? 1.0 : 0.0;
      const double type = (*a)->type == (*b)->type ? 1.0 : 0.0;
      VmMap leftMap{(*a)->fields};
      VmMap rightMap{(*b)->fields};
      return 0.25 * type +
             0.75 * similarityDegreeAtDepth(VmMapPtr(&leftMap, [](VmMap *) {}),
                                            VmMapPtr(&rightMap, [](VmMap *) {}),
                                            depth + 1);
    }
  }
  return vmValuesEqualAtDepth(left, right, depth) ? 1.0 : 0.0;
}

double similarityDegree(const VmValue &left, const VmValue &right) {
  return similarityDegreeAtDepth(left, right, 0);
}

bool validVmValue(const VmValue &value, std::size_t depth = 0) {
  constexpr std::size_t kMaximumValueDepth = 256;
  if (depth > kMaximumValueDepth)
    return false;
  if (const auto number = std::get_if<double>(&value))
    return std::isfinite(*number);
  if (const auto degree = std::get_if<VmDegree>(&value))
    return std::isfinite(degree->value) && degree->value >= 0.0 &&
           degree->value <= 1.0;
  if (const auto *array = std::get_if<VmArrayPtr>(&value)) {
    if (!*array)
      return false;
    return std::all_of(
        (*array)->values.begin(), (*array)->values.end(),
        [&](const VmValue &item) { return validVmValue(item, depth + 1); });
  }
  if (const auto *map = std::get_if<VmMapPtr>(&value)) {
    if (!*map)
      return false;
    return std::all_of((*map)->entries.begin(), (*map)->entries.end(),
                       [&](const auto &entry) {
                         return validVmValue(entry.second, depth + 1);
                       });
  }
  if (const auto *fact = std::get_if<VmFactPtr>(&value)) {
    if (!*fact)
      return false;
    return std::all_of((*fact)->fields.begin(), (*fact)->fields.end(),
                       [&](const auto &entry) {
                         return validVmValue(entry.second, depth + 1);
                       });
  }
  return true;
}

bool validSemanticInputs(std::uint16_t operation,
                         std::span<const VmValue> inputs) {
  if (!semanticOperationAcceptsArity(operation, inputs.size()) ||
      !validVmValue(inputs.front()))
    return false;
  switch (static_cast<SemanticOperationId>(operation)) {
  case SemanticOperationId::Identity:
    return true;
  case SemanticOperationId::SelectFact:
  case SemanticOperationId::DeriveFact: {
    const auto fact = std::get_if<VmFactPtr>(&inputs.front());
    return fact && static_cast<bool>(*fact);
  }
  case SemanticOperationId::EvaluateDegree:
    return std::holds_alternative<double>(inputs.front()) ||
           std::holds_alternative<VmDegree>(inputs.front());
  }
  return false;
}

bool validSemanticOutput(std::uint16_t operation,
                         std::span<const VmValue> inputs,
                         const VmValue &output) {
  if (!validVmValue(output))
    return false;
  switch (static_cast<SemanticOperationId>(operation)) {
  case SemanticOperationId::Identity:
    return runtimeValueKind(output) == runtimeValueKind(inputs.front());
  case SemanticOperationId::SelectFact:
  case SemanticOperationId::DeriveFact:
    return std::holds_alternative<VmNil>(output) ||
           std::holds_alternative<VmFactPtr>(output);
  case SemanticOperationId::EvaluateDegree:
    return std::holds_alternative<VmDegree>(output);
  }
  return false;
}
void verifyControlFlowInitialization(
    const FelidaeIr &ir, const std::unordered_set<std::size_t> &boundaries) {
  std::vector<std::optional<std::vector<bool>>> incoming(ir.words.size());
  std::deque<std::size_t> pending;
  incoming[0] = std::vector<bool>(ir.registerCount, false);
  pending.push_back(0);
  const auto enqueue =
      [&](std::size_t target, const std::vector<bool> &state,
          std::deque<std::size_t> &queue,
          std::vector<std::optional<std::vector<bool>>> &states) {
        if (!boundaries.contains(target))
          return;
        if (!states[target]) {
          states[target] = state;
          queue.push_back(target);
          return;
        }
        auto merged = *states[target];
        bool changed = false;
        for (std::size_t index = 0; index < merged.size(); ++index) {
          const bool value = merged[index] && state[index];
          changed = changed || value != merged[index];
          merged[index] = value;
        }
        if (changed) {
          states[target] = std::move(merged);
          queue.push_back(target);
        }
      };
  while (!pending.empty()) {
    const auto pc = pending.front();
    pending.pop_front();
    auto state = *incoming[pc];
    const auto op = static_cast<IrOpcode>(ir.words[pc]);
    const auto next = pc + irInstructionWidth(ir, pc);
    auto write = [&](IrWord target) { state[target] = true; };
    switch (op) {
    case IrOpcode::LoadConst:
    case IrOpcode::LoadSymbol:
    case IrOpcode::ForEachFact:
    case IrOpcode::TemporalRank:
      write(ir.words[pc + 1]);
      break;
    case IrOpcode::StoreSymbol:
      requireFlowRead(state, ir.words[pc + 2]);
      break;
    case IrOpcode::Move:
      requireFlowRead(state, ir.words[pc + 2]);
      write(ir.words[pc + 1]);
      break;
    case IrOpcode::Add:
    case IrOpcode::Sub:
    case IrOpcode::Mul:
    case IrOpcode::Div:
    case IrOpcode::Mod:
    case IrOpcode::Similarity:
    case IrOpcode::Membership:
    case IrOpcode::HierarchyIsA:
    case IrOpcode::HierarchyCommonAncestors:
    case IrOpcode::HierarchyLeastCommonAncestors:
    case IrOpcode::HierarchyMostGeneralAncestors:
    case IrOpcode::Compare:
      requireFlowRead(state, ir.words[pc + 2]);
      requireFlowRead(state, ir.words[pc + 3]);
      write(ir.words[pc + 1]);
      break;
    case IrOpcode::JumpIfFalse:
      requireFlowRead(state, ir.words[pc + 1]);
      break;
    case IrOpcode::Call:
      for (std::size_t i = 0; i < ir.words[pc + 3]; ++i)
        requireFlowRead(state, ir.words[pc + 4 + i]);
      write(ir.words[pc + 1]);
      break;
    case IrOpcode::SemanticEval:
      for (std::size_t i = 0; i < ir.words[pc + 3]; ++i)
        requireFlowRead(state, ir.words[pc + 4 + i]);
      write(ir.words[pc + 1]);
      break;
    case IrOpcode::Numeric:
      for (std::size_t i = 0; i < ir.words[pc + 3]; ++i)
        requireFlowRead(state, ir.words[pc + 4 + i]);
      write(ir.words[pc + 1]);
      break;
    case IrOpcode::CallNamed:
      for (std::size_t i = 0; i < ir.words[pc + 3]; ++i)
        requireFlowRead(state, ir.words[pc + 5 + 2 * i]);
      write(ir.words[pc + 1]);
      break;
    case IrOpcode::MakeArray:
      for (std::size_t i = 0; i < ir.words[pc + 3]; ++i)
        requireFlowRead(state, ir.words[pc + 4 + i]);
      write(ir.words[pc + 1]);
      break;
    case IrOpcode::MakeMap:
      for (std::size_t i = 0; i < ir.words[pc + 3]; ++i)
        requireFlowRead(state, ir.words[pc + 5 + 2 * i]);
      write(ir.words[pc + 1]);
      break;
    case IrOpcode::GetField:
      requireFlowRead(state, ir.words[pc + 2]);
      write(ir.words[pc + 1]);
      break;
    case IrOpcode::SetField:
      requireFlowRead(state, ir.words[pc + 1]);
      requireFlowRead(state, ir.words[pc + 3]);
      break;
    case IrOpcode::Return:
      requireFlowRead(state, ir.words[pc + 1]);
      break;
    case IrOpcode::CallNative:
    case IrOpcode::MakeFact:
      write(ir.words[pc + 1]);
      break;
    case IrOpcode::End:
    case IrOpcode::Jump:
      break;
    case IrOpcode::Count:
      throw IrError("IR control-flow scan found an invalid opcode");
    }
    if (op == IrOpcode::End || op == IrOpcode::Return)
      continue;
    if (op == IrOpcode::Jump) {
      enqueue(ir.words[pc + 1], state, pending, incoming);
    } else if (op == IrOpcode::JumpIfFalse) {
      enqueue(ir.words[pc + 2], state, pending, incoming);
      enqueue(next, state, pending, incoming);
    } else {
      enqueue(next, state, pending, incoming);
    }
  }
}
} // namespace

std::string vmValueToDisplayString(const VmValue &value,
                                   const VmDisplayContext &context) {
  const auto renderNumber = [](double number) {
    std::ostringstream out;
    out.precision(15);
    out << number;
    auto result = out.str();
    if ((number == 0.0 || number == 1.0) &&
        result.find_first_of(".eE") == std::string::npos) {
      result += ".0";
    }
    return result;
  };
  const auto render = [&](const auto &self,
                          const VmValue &item) -> std::string {
    if (std::holds_alternative<VmNil>(item))
      return "nil";
    if (const auto number = std::get_if<double>(&item))
      return renderNumber(*number);
    if (const auto degree = std::get_if<VmDegree>(&item))
      return renderNumber(degree->value);
    if (const auto text = std::get_if<VmText>(&item)) {
      if (!context.textDecoder)
        return "<text:" + std::to_string(text->pieces.size()) + " pieces>";
      return context.textDecoder(text->pieces);
    }
    if (const auto symbol = std::get_if<VmSymbol>(&item)) {
      auto name = context.symbolDecoder ? context.symbolDecoder(symbol->value)
                                        : std::string{};
      return name.empty() ? "#" + std::to_string(symbol->value) : name;
    }
    if (const auto array = std::get_if<VmArrayPtr>(&item)) {
      if (!*array)
        throw IrError("VM display received an invalid array value");
      std::ostringstream out;
      out << "[";
      for (std::size_t index = 0; index < (*array)->values.size(); ++index) {
        if (index)
          out << ", ";
        out << self(self, (*array)->values[index]);
      }
      return out << "]", out.str();
    }
    const auto renderFields = [&](const auto &fields) {
      std::ostringstream out;
      out << "{";
      for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index)
          out << ", ";
        auto name = context.symbolDecoder
                        ? context.symbolDecoder(fields[index].first)
                        : std::string{};
        if (name.empty())
          name = "#" + std::to_string(fields[index].first);
        out << name << ": " << self(self, fields[index].second);
      }
      return out << "}", out.str();
    };
    if (const auto map = std::get_if<VmMapPtr>(&item)) {
      if (!*map)
        throw IrError("VM display received an invalid map value");
      return renderFields((*map)->entries);
    }
    if (const auto fact = std::get_if<VmFactPtr>(&item)) {
      if (!*fact)
        throw IrError("VM display received an invalid fact value");
      return renderFields((*fact)->fields);
    }
    throw IrError("Direct VM result cannot display an opaque runtime value");
  };
  return render(render, value);
}

void VmFactStore::registerType(IrSymbolRef type,
                               std::vector<IrSymbolRef> parents) {
  if (type == 0)
    throw IrError("fact hierarchy type is invalid");
  std::lock_guard lock(mutex_);
  if (const auto existing = parents_.find(type); existing != parents_.end()) {
    if (existing->second != parents)
      throw IrError("fact hierarchy type has conflicting parents");
    return;
  }
  for (const auto parent : parents) {
    if (parent == 0 || parent == type)
      throw IrError("fact hierarchy parent is invalid");
  }
  parents_.emplace(type, std::move(parents));
  ++revision_;
}

IrFactRef VmFactStore::retain(const VmFactPtr &fact) {
  if (!fact)
    throw IrError("fact store cannot retain a null fact");
  std::lock_guard lock(mutex_);
  if (fact->id == 0) {
    if (nextId_ == std::numeric_limits<IrFactRef>::max()) {
      throw IrError("fact store exhausted its fact ID space");
    }
    fact->id = nextId_++;
    fact->createdSequence = nextSequence_++;
    facts_.push_back(fact);
    byType_[fact->type].push_back(fact);
    for (const auto &[field, _] : fact->fields)
      byField_[field].push_back(fact);
    provenance_.push_back(
        VmFactProvenance{fact->id, 0, fact->origin == VmFact::Origin::Derived});
    ++revision_;
  }
  return fact->id;
}

void VmFactStore::mutate(const VmFactPtr &fact, IrSymbolRef field,
                         const VmValue &value, IrSymbolRef procedure) {
  if (!fact || fact->id == 0 || field == 0)
    throw IrError("fact mutation is invalid");
  std::lock_guard lock(mutex_);
  const auto known =
      std::find_if(facts_.begin(), facts_.end(),
                   [&](const auto &item) { return item == fact; });
  if (known == facts_.end())
    throw IrError(
        "fact mutation targets a fact outside this knowledge runtime");
  bool hadField = false;
  for (auto &[existing, previous] : fact->fields) {
    if (existing == field) {
      previous = value;
      hadField = true;
      break;
    }
  }
  if (!hadField) {
    fact->fields.emplace_back(field, value);
    byField_[field].push_back(fact);
  }
  mutations_.push_back(VmFactMutation{nextSequence_++, fact->id, field});
  provenance_.push_back(VmFactProvenance{
      fact->id, procedure, fact->origin == VmFact::Origin::Derived});
  ++revision_;
}

std::vector<VmFactPtr> VmFactStore::snapshot() const {
  std::lock_guard lock(mutex_);
  return facts_;
}

std::vector<VmFactPtr> VmFactStore::snapshot(IrSymbolRef type) const {
  std::lock_guard lock(mutex_);
  const auto found = byType_.find(type);
  return found == byType_.end() ? std::vector<VmFactPtr>{} : found->second;
}

std::vector<IrSymbolRef>
VmFactStore::hierarchyProof(IrSymbolRef child, IrSymbolRef ancestor) const {
  std::lock_guard lock(mutex_);
  if (child == 0 || ancestor == 0)
    return {};
  std::vector<std::pair<IrSymbolRef, std::vector<IrSymbolRef>>> pending{
      {child, {child}}};
  std::unordered_set<IrSymbolRef> visited;
  while (!pending.empty()) {
    auto [current, proof] = std::move(pending.back());
    pending.pop_back();
    if (!visited.insert(current).second)
      continue;
    if (current == ancestor)
      return proof;
    const auto found = parents_.find(current);
    if (found == parents_.end())
      continue;
    for (const auto parent : found->second) {
      auto next = proof;
      next.push_back(parent);
      pending.emplace_back(parent, std::move(next));
    }
  }
  return {};
}

std::vector<IrSymbolRef> VmFactStore::commonAncestors(IrSymbolRef left,
                                                      IrSymbolRef right) const {
  std::lock_guard lock(mutex_);
  if (left == 0 || right == 0)
    return {};
  const auto closure = [&](IrSymbolRef type) {
    std::unordered_set<IrSymbolRef> result;
    std::vector<IrSymbolRef> pending{type};
    while (!pending.empty()) {
      const auto current = pending.back();
      pending.pop_back();
      if (!result.insert(current).second)
        continue;
      if (const auto found = parents_.find(current); found != parents_.end()) {
        pending.insert(pending.end(), found->second.begin(),
                       found->second.end());
      }
    }
    return result;
  };
  const auto leftAncestors = closure(left);
  const auto rightAncestors = closure(right);
  std::vector<IrSymbolRef> result;
  for (const auto candidate : leftAncestors)
    if (rightAncestors.contains(candidate))
      result.push_back(candidate);
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<IrSymbolRef>
VmFactStore::leastCommonAncestors(IrSymbolRef left, IrSymbolRef right) const {
  const auto common = commonAncestors(left, right);
  std::vector<IrSymbolRef> result;
  for (const auto candidate : common) {
    bool hasMoreSpecificCommon = false;
    for (const auto other : common) {
      if (candidate == other)
        continue;
      if (!hierarchyProof(other, candidate).empty()) {
        hasMoreSpecificCommon = true;
        break;
      }
    }
    if (!hasMoreSpecificCommon)
      result.push_back(candidate);
  }
  return result;
}

std::vector<IrSymbolRef>
VmFactStore::mostGeneralCommonAncestors(IrSymbolRef left,
                                        IrSymbolRef right) const {
  const auto common = commonAncestors(left, right);
  std::vector<IrSymbolRef> result;
  for (const auto candidate : common) {
    bool hasMoreGeneralCommon = false;
    for (const auto other : common) {
      if (candidate == other)
        continue;
      if (!hierarchyProof(candidate, other).empty()) {
        hasMoreGeneralCommon = true;
        break;
      }
    }
    if (!hasMoreGeneralCommon)
      result.push_back(candidate);
  }
  return result;
}

std::vector<VmRankedFact>
VmFactStore::rankByTimeAndPriority(IrSymbolRef effectiveAtField,
                                   IrSymbolRef priorityField) const {
  if (effectiveAtField == 0 || priorityField == 0)
    throw IrError("fact ranking fields are invalid");
  std::lock_guard lock(mutex_);
  const auto numberField = [](const VmFactPtr &fact, IrSymbolRef field,
                              const char *label) {
    const auto found =
        std::find_if(fact->fields.begin(), fact->fields.end(),
                     [&](const auto &item) { return item.first == field; });
    if (found == fact->fields.end())
      throw IrError(std::string("fact has no ") + label + " field");
    const auto number = std::get_if<double>(&found->second);
    if (!number || !std::isfinite(*number))
      throw IrError(std::string("fact ") + label +
                    " field is not finite numeric data");
    return *number;
  };
  std::vector<VmRankedFact> ranked;
  ranked.reserve(facts_.size());
  for (const auto &fact : facts_)
    ranked.push_back({fact, numberField(fact, effectiveAtField, "effective-at"),
                      numberField(fact, priorityField, "priority")});
  std::sort(ranked.begin(), ranked.end(),
            [](const auto &left, const auto &right) {
              if (left.effectiveAt != right.effectiveAt)
                return left.effectiveAt > right.effectiveAt;
              if (left.priority != right.priority)
                return left.priority > right.priority;
              return left.fact->id < right.fact->id;
            });
  return ranked;
}

std::vector<VmFactPtr>
VmFactStore::snapshotAssignableTo(IrSymbolRef type) const {
  std::lock_guard lock(mutex_);
  if (const auto cached = assignableCache_.find(type);
      cached != assignableCache_.end() && cached->second.first == revision_) {
    return cached->second.second;
  }
  std::vector<VmFactPtr> result;
  for (const auto &[candidate, facts] : byType_) {
    bool matches = candidate == type;
    if (!matches) {
      std::vector<IrSymbolRef> pending{candidate};
      std::unordered_set<IrSymbolRef> visited;
      while (!pending.empty() && !matches) {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second)
          continue;
        const auto found = parents_.find(current);
        if (found == parents_.end())
          continue;
        for (const auto parent : found->second) {
          if (parent == type) {
            matches = true;
            break;
          }
          pending.push_back(parent);
        }
      }
    }
    if (matches)
      result.insert(result.end(), facts.begin(), facts.end());
  }
  assignableCache_[type] = {revision_, result};
  return result;
}

std::vector<VmFactPtr> VmFactStore::snapshotByField(IrSymbolRef field) const {
  std::lock_guard lock(mutex_);
  const auto found = byField_.find(field);
  return found == byField_.end() ? std::vector<VmFactPtr>{} : found->second;
}

std::vector<VmFactMutation> VmFactStore::mutations() const {
  std::lock_guard lock(mutex_);
  return mutations_;
}
std::vector<VmFactProvenance> VmFactStore::provenance() const {
  std::lock_guard lock(mutex_);
  return provenance_;
}

VmKnowledgeSnapshot VmFactStore::knowledgeSnapshot() const {
  std::uint64_t revision = std::numeric_limits<std::uint64_t>::max();
  VmKnowledgeSnapshot result;
  refreshKnowledgeSnapshot(revision, result);
  return result;
}

void VmFactStore::refreshKnowledgeSnapshot(std::uint64_t &knownRevision,
                                           VmKnowledgeSnapshot &result) const {
  std::lock_guard lock(mutex_);
  if (knownRevision == revision_)
    return;
  result = {};
  result.factTypes.reserve(byType_.size());
  result.factTypeCounts.reserve(byType_.size());
  for (const auto &[type, facts] : byType_) {
    result.factTypes.push_back(type);
    result.factTypeCounts.emplace_back(
        type,
        static_cast<std::uint32_t>(std::min(
            facts.size(), static_cast<std::size_t>(
                              std::numeric_limits<std::uint32_t>::max()))));
  }
  std::sort(result.factTypes.begin(), result.factTypes.end());
  std::sort(result.factTypeCounts.begin(), result.factTypeCounts.end());
  for (const auto &[child, parents] : parents_) {
    for (const auto parent : parents)
      result.hierarchyEdges.emplace_back(child, parent);
  }
  std::sort(result.hierarchyEdges.begin(), result.hierarchyEdges.end());
  knownRevision = revision_;
}

std::size_t VmFactStore::size() const {
  std::lock_guard lock(mutex_);
  return facts_.size();
}

double VmFactStore::gaussianMembership(double value,
                                       const VmGaussianProfile &profile) {
  constexpr double kTailMembership = 0.01;
  if (!std::isfinite(value) || !std::isfinite(profile.peak) ||
      !std::isfinite(profile.fadesIn) || !std::isfinite(profile.fadesOut) ||
      profile.fadesIn > profile.peak || profile.peak > profile.fadesOut) {
    throw IrError("Gaussian profile is invalid");
  }
  const auto sigmaFor = [](double distance) {
    return distance / std::sqrt(-2.0 * std::log(kTailMembership));
  };
  const auto leftDistance = profile.peak - profile.fadesIn;
  const auto rightDistance = profile.fadesOut - profile.peak;
  const auto fallback = std::max(leftDistance, rightDistance);
  if (fallback == 0.0)
    return value == profile.peak ? 1.0 : 0.0;
  const auto sigma =
      value <= profile.peak
          ? sigmaFor(leftDistance == 0.0 ? fallback : leftDistance)
          : sigmaFor(rightDistance == 0.0 ? fallback : rightDistance);
  const auto z = (value - profile.peak) / sigma;
  return std::exp(-0.5 * z * z);
}

FelidaeKnowledgeRuntime::FelidaeKnowledgeRuntime(
    RuntimeStateModel *semanticModel, std::size_t maximumSemanticSteps,
    std::size_t maximumCallDepth, std::shared_ptr<VmFactStore> factStore)
    : factStore_(factStore ? std::move(factStore)
                           : std::make_shared<VmFactStore>()),
      semanticModel_(semanticModel),
      maximumSemanticSteps_(maximumSemanticSteps),
      maximumCallDepth_(maximumCallDepth) {
  if (maximumSemanticSteps_ == 0)
    throw IrError("direct VM semantic step limit must be positive");
  if (maximumCallDepth_ == 0)
    throw IrError("direct VM call depth limit must be positive");
}

void FelidaeKnowledgeRuntime::retainFact(const VmFactPtr &fact) {
  (void)factStore_->retain(fact);
}

void FelidaeKnowledgeRuntime::mutateFact(const VmFactPtr &fact,
                                         IrSymbolRef field,
                                         const VmValue &value) {
  factStore_->mutate(fact, field, value,
                     callFrames_.empty() ? 0 : callFrames_.back().procedure);
}

void FelidaeKnowledgeRuntime::registerFactType(
    IrSymbolRef type, std::vector<IrSymbolRef> parents) {
  factStore_->registerType(type, std::move(parents));
}

std::vector<VmFactPtr>
FelidaeKnowledgeRuntime::snapshotFacts(IrSymbolRef type) {
  return factStore_->snapshotAssignableTo(type);
}

std::vector<IrSymbolRef>
FelidaeKnowledgeRuntime::hierarchyProof(IrSymbolRef child,
                                        IrSymbolRef ancestor) {
  return factStore_->hierarchyProof(child, ancestor);
}
std::vector<IrSymbolRef>
FelidaeKnowledgeRuntime::commonAncestors(IrSymbolRef left, IrSymbolRef right) {
  return factStore_->commonAncestors(left, right);
}
std::vector<IrSymbolRef>
FelidaeKnowledgeRuntime::leastCommonAncestors(IrSymbolRef left,
                                              IrSymbolRef right) {
  return factStore_->leastCommonAncestors(left, right);
}
std::vector<IrSymbolRef>
FelidaeKnowledgeRuntime::mostGeneralCommonAncestors(IrSymbolRef left,
                                                    IrSymbolRef right) {
  return factStore_->mostGeneralCommonAncestors(left, right);
}
std::vector<VmRankedFact>
FelidaeKnowledgeRuntime::rankFacts(IrSymbolRef effectiveAtField,
                                   IrSymbolRef priorityField) {
  return factStore_->rankByTimeAndPriority(effectiveAtField, priorityField);
}

void FelidaeKnowledgeRuntime::installIrModule(const IrModule &module) {
  module_ = {};
  module_.symbolTable = module.symbolTable;
}

IrSymbolRef
FelidaeKnowledgeRuntime::resolveSymbol(IrSymbolRef moduleSymbol) const {
  if (module_.symbolTable.empty())
    throw IrError("IR runtime has no installed module");
  (void)irSymbolPieces(module_.symbolTable, moduleSymbol);
  return moduleSymbol;
}

void FelidaeKnowledgeRuntime::enterProcedure(
    IrSymbolRef procedure, std::span<const IrSymbolRef> parameters,
    std::span<const VmValue> arguments) {
  if (parameters.size() != arguments.size())
    throw IrError("IR procedure received the wrong number of arguments");
  if (procedureDepth_ >= maximumCallDepth_)
    throw IrError("IR procedure call depth exceeds its limit");
  ++procedureDepth_;
  VmCallFrame frame;
  frame.procedure = procedure;
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (!frame.locals.emplace(parameters[index], arguments[index]).second) {
      --procedureDepth_;
      throw IrError("IR procedure parameter metadata is duplicated");
    }
  }
  callFrames_.push_back(std::move(frame));
}

void FelidaeKnowledgeRuntime::leaveProcedure() noexcept {
  if (!callFrames_.empty())
    callFrames_.pop_back();
  if (procedureDepth_ != 0)
    --procedureDepth_;
}

VmValue FelidaeKnowledgeRuntime::loadSymbol(IrSymbolRef symbol) {
  // Procedures are lexically isolated: a callee may read its own parameters
  // and locals or module globals, never caller-local bindings.
  if (!callFrames_.empty()) {
    const auto local = callFrames_.back().locals.find(symbol);
    if (local != callFrames_.back().locals.end())
      return local->second;
  }
  if (module_.symbolTable.empty())
    throw IrError("IR runtime has no installed module");
  const auto &globals = module_.globals;
  const auto global = globals.find(symbol);
  if (global == globals.end())
    throw IrError("direct VM runtime reads an undefined symbol");
  return global->second;
}

void FelidaeKnowledgeRuntime::storeSymbol(IrSymbolRef symbol,
                                          const VmValue &value) {
  if (callFrames_.empty()) {
    if (module_.symbolTable.empty())
      throw IrError("IR runtime has no installed module");
    auto &globals = module_.globals;
    if (globals.contains(symbol)) {
      throw IrError("direct VM runtime cannot rebind an immutable global");
    }
    globals.emplace(symbol, value);
    return;
  }
  auto &locals = callFrames_.back().locals;
  if (locals.contains(symbol)) {
    throw IrError("direct VM runtime cannot rebind an immutable local");
  }
  locals.emplace(symbol, value);
}

RuntimeStateModel *FelidaeKnowledgeRuntime::runtimeStateModel() {
  return semanticModel_;
}

void FelidaeKnowledgeRuntime::beginExecution() {
  if (executionDepth_++ == 0) {
    executionState_ =
        semanticModel_ ? semanticModel_->createExecutionState() : nullptr;
    sharedSemanticSteps_ = std::make_shared<std::size_t>(0);
  }
}

void FelidaeKnowledgeRuntime::endExecution() noexcept {
  if (executionDepth_ == 0)
    return;
  if (--executionDepth_ == 0) {
    executionState_.reset();
    sharedSemanticSteps_.reset();
  }
}

RuntimeContext
FelidaeKnowledgeRuntime::makeRuntimeContext(const VmValue &) const {
  RuntimeContext context;
  context.maximumSemanticSteps = maximumSemanticSteps_;
  context.executionState = executionState_;
  context.sharedSemanticSteps = sharedSemanticSteps_;
  context.symbolTable = &module_.symbolTable;
  factStore_->refreshKnowledgeSnapshot(context.knowledgeRevision,
                                       context.knowledge);
  return context;
}

void FelidaeKnowledgeRuntime::refreshRuntimeContext(
    RuntimeContext &context) const {
  factStore_->refreshKnowledgeSnapshot(context.knowledgeRevision,
                                       context.knowledge);
}
void IrVerifier::verify(const FelidaeIr &ir) {
  if (ir.words.empty())
    throw IrError("IR is empty");
  if (ir.words.size() > kMaximumIrWords)
    throw IrError("IR exceeds its word limit");
  if (ir.registerCount > kMaximumRegisters)
    throw IrError("IR exceeds its register limit");
  if (ir.constants.size() > kMaximumIrTableEntries ||
      ir.symbols.size() > kMaximumIrTableEntries ||
      ir.texts.size() > kMaximumIrTableEntries ||
      ir.sourceMap.size() > kMaximumIrTableEntries) {
    throw IrError("IR side table exceeds its entry limit");
  }
  std::size_t textBytes = 0;
  for (const auto &text : ir.texts) {
    if (text.size() > kMaximumTextBytes - textBytes)
      throw IrError("IR text table exceeds its byte limit");
    textBytes += text.size();
  }
  std::unordered_set<std::size_t> boundaries;
  for (std::size_t scan = 0; scan < ir.words.size();) {
    boundaries.insert(scan);
    scan += irInstructionWidth(ir, scan);
  }
  for (const auto &entry : ir.sourceMap) {
    if (!boundaries.contains(entry.instructionWord)) {
      throw IrError("IR source map does not reference an instruction boundary");
    }
    const auto &span = entry.sourceSpan;
    if (span.startLine <= 0 || span.startColumn <= 0 ||
        span.endLine < span.startLine ||
        (span.endLine == span.startLine && span.endColumn < span.startColumn)) {
      throw IrError("IR source map contains an invalid source span");
    }
  }
  std::vector<bool> initialized(ir.registerCount, false);
  std::size_t pc = 0;
  bool ended = false;
  while (pc < ir.words.size()) {
    const auto op = opcodeAt(ir, pc);
    switch (op) {
    case IrOpcode::End:
      if (pc + 1 != ir.words.size())
        throw IrError("IR has instructions after END");
      ended = true;
      ++pc;
      break;
    case IrOpcode::LoadConst:
      requireRegister(ir, ir.words[pc + 1]);
      if (ir.words[pc + 2] >= ir.constants.size())
        throw IrError("IR references an invalid constant");
      if (ir.constants[ir.words[pc + 2]].kind == IrConstantKind::Text &&
          ir.constants[ir.words[pc + 2]].value >= ir.texts.size()) {
        throw IrError("IR references an invalid text constant");
      }
      initialized[ir.words[pc + 1]] = true;
      pc += 3;
      break;
    case IrOpcode::LoadSymbol:
      requireRegister(ir, ir.words[pc + 1]);
      if (ir.words[pc + 2] >= ir.symbols.size())
        throw IrError("IR references an invalid symbol");
      initialized[ir.words[pc + 1]] = true;
      pc += 3;
      break;
    case IrOpcode::StoreSymbol:
      if (ir.words[pc + 1] >= ir.symbols.size())
        throw IrError("IR stores an invalid symbol");
      requireRegister(ir, ir.words[pc + 2]);
      requireInitialized(initialized, ir.words[pc + 2]);
      pc += 3;
      break;
    case IrOpcode::Move:
      requireRegister(ir, ir.words[pc + 1]);
      requireRegister(ir, ir.words[pc + 2]);
      requireInitialized(initialized, ir.words[pc + 2]);
      initialized[ir.words[pc + 1]] = true;
      pc += 3;
      break;
    case IrOpcode::ForEachFact:
      requireRegister(ir, ir.words[pc + 1]);
      if (ir.words[pc + 2] >= ir.symbols.size() ||
          ir.words[pc + 3] >= ir.symbols.size())
        throw IrError("IR fact loop references an invalid symbol");
      initialized[ir.words[pc + 1]] = true;
      pc += 4;
      break;
    case IrOpcode::Add:
    case IrOpcode::Sub:
    case IrOpcode::Mul:
    case IrOpcode::Div:
    case IrOpcode::Mod:
      requireRegister(ir, ir.words[pc + 1]);
      requireRegister(ir, ir.words[pc + 2]);
      requireRegister(ir, ir.words[pc + 3]);
      requireInitialized(initialized, ir.words[pc + 2]);
      requireInitialized(initialized, ir.words[pc + 3]);
      initialized[ir.words[pc + 1]] = true;
      pc += 4;
      break;
    case IrOpcode::Similarity:
    case IrOpcode::HierarchyIsA:
    case IrOpcode::HierarchyCommonAncestors:
    case IrOpcode::HierarchyLeastCommonAncestors:
    case IrOpcode::HierarchyMostGeneralAncestors:
      requireRegister(ir, ir.words[pc + 1]);
      requireRegister(ir, ir.words[pc + 2]);
      requireRegister(ir, ir.words[pc + 3]);
      requireInitialized(initialized, ir.words[pc + 2]);
      requireInitialized(initialized, ir.words[pc + 3]);
      initialized[ir.words[pc + 1]] = true;
      pc += 4;
      break;
    case IrOpcode::TemporalRank:
      requireRegister(ir, ir.words[pc + 1]);
      if (ir.words[pc + 2] >= ir.symbols.size() ||
          ir.words[pc + 3] >= ir.symbols.size()) {
        throw IrError("IR temporal rank references an invalid field symbol");
      }
      initialized[ir.words[pc + 1]] = true;
      pc += 4;
      break;
    case IrOpcode::Membership:
      for (std::size_t i = 1; i < 6; ++i) {
        requireRegister(ir, ir.words[pc + i]);
        if (i > 1)
          requireInitialized(initialized, ir.words[pc + i]);
      }
      initialized[ir.words[pc + 1]] = true;
      pc += 6;
      break;
    case IrOpcode::Compare:
      requireRegister(ir, ir.words[pc + 1]);
      requireRegister(ir, ir.words[pc + 2]);
      requireRegister(ir, ir.words[pc + 3]);
      requireInitialized(initialized, ir.words[pc + 2]);
      requireInitialized(initialized, ir.words[pc + 3]);
      if (ir.words[pc + 4] > static_cast<IrWord>(IrComparison::GreaterEqual)) {
        throw IrError("IR comparison kind is invalid");
      }
      initialized[ir.words[pc + 1]] = true;
      pc += 5;
      break;
    case IrOpcode::Jump:
      if (!boundaries.contains(ir.words[pc + 1]))
        throw IrError("IR jump target is not an instruction boundary");
      pc += 2;
      break;
    case IrOpcode::JumpIfFalse:
      requireRegister(ir, ir.words[pc + 1]);
      requireInitialized(initialized, ir.words[pc + 1]);
      if (!boundaries.contains(ir.words[pc + 2]))
        throw IrError("IR jump target is not an instruction boundary");
      pc += 3;
      break;
    case IrOpcode::CallNative:
      requireRegister(ir, ir.words[pc + 1]);
      if (ir.words[pc + 2] >= ir.symbols.size())
        throw IrError("IR call references an invalid symbol");
      initialized[ir.words[pc + 1]] = true;
      pc += 3;
      break;
    case IrOpcode::Call: {
      requireRegister(ir, ir.words[pc + 1]);
      if (ir.words[pc + 2] >= ir.symbols.size())
        throw IrError("IR call references an invalid symbol");
      const auto count = ir.words[pc + 3];
      const auto width = irInstructionWidth(ir, pc);
      for (std::size_t index = 0; index < count; ++index) {
        requireRegister(ir, ir.words[pc + 4 + index]);
        requireInitialized(initialized, ir.words[pc + 4 + index]);
      }
      initialized[ir.words[pc + 1]] = true;
      pc += width;
      break;
    }
    case IrOpcode::SemanticEval: {
      requireRegister(ir, ir.words[pc + 1]);
      if (ir.words[pc + 2] > std::numeric_limits<std::uint16_t>::max() ||
          !isKnownSemanticOperation(
              static_cast<std::uint16_t>(ir.words[pc + 2]))) {
        throw IrError("compiler IR semantic operation ID is invalid");
      }
      const auto count = ir.words[pc + 3];
      if (!semanticOperationAcceptsArity(
              static_cast<std::uint16_t>(ir.words[pc + 2]), count)) {
        throw IrError("compiler IR semantic operation arity is invalid");
      }
      const auto width = irInstructionWidth(ir, pc);
      for (std::size_t index = 0; index < count; ++index) {
        requireRegister(ir, ir.words[pc + 4 + index]);
        requireInitialized(initialized, ir.words[pc + 4 + index]);
      }
      initialized[ir.words[pc + 1]] = true;
      pc += width;
      break;
    }
    case IrOpcode::Numeric: {
      requireRegister(ir, ir.words[pc + 1]);
      if (ir.words[pc + 2] >= static_cast<IrWord>(NumericOperation::Count)) {
        throw IrError("IR numeric operation ID is invalid");
      }
      const auto operation = static_cast<NumericOperation>(ir.words[pc + 2]);
      const auto count = ir.words[pc + 3];
      if (count != numericOperationArity(operation))
        throw IrError("IR numeric operation arity is invalid");
      const auto width = irInstructionWidth(ir, pc);
      for (std::size_t index = 0; index < count; ++index) {
        requireRegister(ir, ir.words[pc + 4 + index]);
        requireInitialized(initialized, ir.words[pc + 4 + index]);
      }
      initialized[ir.words[pc + 1]] = true;
      pc += width;
      break;
    }
    case IrOpcode::CallNamed: {
      requireRegister(ir, ir.words[pc + 1]);
      if (ir.words[pc + 2] >= ir.symbols.size())
        throw IrError("IR call references an invalid symbol");
      const auto count = ir.words[pc + 3];
      const auto width = irInstructionWidth(ir, pc);
      for (std::size_t index = 0; index < count; ++index) {
        const auto name = ir.words[pc + 4 + 2 * index];
        const auto value = ir.words[pc + 5 + 2 * index];
        if (name != 0 && name - 1 >= ir.symbols.size())
          throw IrError("IR call references an invalid argument name");
        requireRegister(ir, value);
        requireInitialized(initialized, value);
      }
      initialized[ir.words[pc + 1]] = true;
      pc += width;
      break;
    }
    case IrOpcode::MakeArray: {
      requireRegister(ir, ir.words[pc + 1]);
      const auto count = ir.words[pc + 3];
      const auto width = irInstructionWidth(ir, pc);
      for (std::size_t index = 0; index < count; ++index) {
        requireRegister(ir, ir.words[pc + 4 + index]);
        requireInitialized(initialized, ir.words[pc + 4 + index]);
      }
      initialized[ir.words[pc + 1]] = true;
      pc += width;
      break;
    }
    case IrOpcode::MakeMap: {
      requireRegister(ir, ir.words[pc + 1]);
      const auto count = ir.words[pc + 3];
      const auto width = irInstructionWidth(ir, pc);
      for (std::size_t index = 0; index < count; ++index) {
        const auto symbol = ir.words[pc + 4 + 2 * index];
        const auto value = ir.words[pc + 5 + 2 * index];
        if (symbol >= ir.symbols.size())
          throw IrError("IR map references an invalid field symbol");
        requireRegister(ir, value);
        requireInitialized(initialized, value);
      }
      initialized[ir.words[pc + 1]] = true;
      pc += width;
      break;
    }
    case IrOpcode::MakeFact:
      requireRegister(ir, ir.words[pc + 1]);
      if (ir.words[pc + 2] >= ir.symbols.size())
        throw IrError("IR fact references an invalid symbol");
      initialized[ir.words[pc + 1]] = true;
      pc += 3;
      break;
    case IrOpcode::GetField:
      requireRegister(ir, ir.words[pc + 1]);
      requireRegister(ir, ir.words[pc + 2]);
      requireInitialized(initialized, ir.words[pc + 2]);
      if (ir.words[pc + 3] >= ir.symbols.size())
        throw IrError("IR field references an invalid symbol");
      initialized[ir.words[pc + 1]] = true;
      pc += 4;
      break;
    case IrOpcode::SetField:
      requireRegister(ir, ir.words[pc + 1]);
      requireRegister(ir, ir.words[pc + 3]);
      requireInitialized(initialized, ir.words[pc + 1]);
      requireInitialized(initialized, ir.words[pc + 3]);
      if (ir.words[pc + 2] >= ir.symbols.size())
        throw IrError("IR field references an invalid symbol");
      pc += 4;
      break;
    case IrOpcode::Return:
      requireRegister(ir, ir.words[pc + 1]);
      requireInitialized(initialized, ir.words[pc + 1]);
      if (ir.words[pc + 2] != 0)
        throw IrError("IR RETURN reserved operand must be zero");
      pc += 3;
      break;
    default:
      throw IrError("IR opcode is not implemented by this VM build");
    }
    if (ended)
      break;
  }
  if (!ended)
    throw IrError("IR is missing END");
  verifyControlFlowInitialization(ir, boundaries);
}
IrConstant encodeIrNumber(double value) noexcept {
  return std::bit_cast<IrConstant>(value);
}

double decodeIrNumber(IrConstant word) noexcept {
  return std::bit_cast<double>(word);
}

bool VmRuntime::shouldBranchFalse(const VmValue &value) const {
  if (std::holds_alternative<VmNil>(value))
    return true;
  if (const auto *control = std::get_if<double>(&value)) {
    if (*control == 0.0)
      return true;
    if (*control == 1.0)
      return false;
    throw IrError("VM control value must be exactly 0.0 or 1.0");
  }
  throw IrError("VM branch requires a numeric 0.0 or 1.0 control value");
}

VmValue VmRuntime::loadSymbol(IrSymbolRef) {
  throw IrError("IR symbol loading is unavailable in this runtime");
}

void VmRuntime::storeSymbol(IrSymbolRef, const VmValue &) {
  throw IrError("IR symbol storage is unavailable in this runtime");
}

VmValue VmRuntime::callNativeSymbol(IrSymbolRef) {
  throw IrError("IR native call is unavailable in this runtime");
}

void VmRuntime::retainFact(const VmFactPtr &) {}

void VmRuntime::mutateFact(const VmFactPtr &fact, IrSymbolRef field,
                           const VmValue &value) {
  if (!fact)
    throw IrError("IR mutation requires a fact value");
  for (auto &[existing, previous] : fact->fields) {
    if (existing == field) {
      previous = value;
      return;
    }
  }
  fact->fields.emplace_back(field, value);
}

void VmRuntime::registerFactType(IrSymbolRef, std::vector<IrSymbolRef>) {}

std::vector<VmFactPtr> VmRuntime::snapshotFacts(IrSymbolRef) {
  throw IrError("IR fact iteration is unavailable in this runtime");
}

std::vector<IrSymbolRef> VmRuntime::hierarchyProof(IrSymbolRef, IrSymbolRef) {
  throw IrError("IR hierarchy is unavailable in this runtime");
}
std::vector<IrSymbolRef> VmRuntime::commonAncestors(IrSymbolRef, IrSymbolRef) {
  throw IrError("IR hierarchy is unavailable in this runtime");
}
std::vector<IrSymbolRef> VmRuntime::leastCommonAncestors(IrSymbolRef,
                                                         IrSymbolRef) {
  throw IrError("IR hierarchy is unavailable in this runtime");
}
std::vector<IrSymbolRef> VmRuntime::mostGeneralCommonAncestors(IrSymbolRef,
                                                               IrSymbolRef) {
  throw IrError("IR hierarchy is unavailable in this runtime");
}
std::vector<VmRankedFact> VmRuntime::rankFacts(IrSymbolRef, IrSymbolRef) {
  throw IrError("IR temporal ranking is unavailable in this runtime");
}

void VmRuntime::installIrModule(const IrModule &) {}
IrSymbolRef VmRuntime::resolveSymbol(IrSymbolRef moduleSymbol) const {
  return moduleSymbol;
}

void VmRuntime::enterProcedure(IrSymbolRef,
                               std::span<const IrSymbolRef> parameters,
                               std::span<const VmValue> arguments) {
  if (parameters.size() != arguments.size())
    throw IrError("IR procedure received the wrong number of arguments");
}

void VmRuntime::leaveProcedure() noexcept {}

RuntimeStateModel *VmRuntime::runtimeStateModel() { return nullptr; }

void VmRuntime::beginExecution() {}

void VmRuntime::endExecution() noexcept {}

RuntimeContext VmRuntime::makeRuntimeContext(const VmValue &) const {
  return {};
}

void VmRuntime::refreshRuntimeContext(RuntimeContext &) const {}

RegisterVm::RegisterVm(std::size_t maximumInstructionSteps)
    : maximumInstructionSteps_(maximumInstructionSteps) {
  if (maximumInstructionSteps_ == 0) {
    throw IrError("IR instruction-step limit must be positive");
  }
}

VmValue RegisterVm::executeMain(const VerifiedIrModule &verified,
                                VmRuntime &runtime, VmValue systemInput) {
  const auto &module = verified.get();
  runtime.installIrModule(module);
  for (const auto &type : module.factTypes) {
    std::vector<IrSymbolRef> parents;
    parents.reserve(type.parents.size());
    for (const auto parent : type.parents)
      parents.push_back(runtime.resolveSymbol(parent));
    runtime.registerFactType(runtime.resolveSymbol(type.symbol),
                             std::move(parents));
  }
  runtime.beginExecution();
  struct Scope {
    VmRuntime &runtime;
    ~Scope() { runtime.endExecution(); }
  } scope{runtime};
  std::size_t instructionSteps = 0;
  auto result = executeIrProgram(module, module.ir, runtime,
                                 std::move(systemInput), 0, instructionSteps);
  return result;
}

VmValue RegisterVm::executeIrProgram(const IrModule &module,
                                     const FelidaeIr &program,
                                     VmRuntime &runtime, VmValue systemInput,
                                     std::size_t callDepth,
                                     std::size_t &instructionSteps) {
  if (callDepth > 256)
    throw IrError("IR procedure call depth exceeds VM limit");
  std::vector<VmValue> registers(program.registerCount, VmNil{});
  auto semanticContext = runtime.makeRuntimeContext(systemInput);
  const auto moduleSymbol = [&](IrWord index) {
    return program.symbols.at(index);
  };
  const auto symbol = [&](IrWord index) {
    return runtime.resolveSymbol(moduleSymbol(index));
  };
  const auto typeSymbol = [&](const VmValue &value) -> IrSymbolRef {
    if (const auto item = std::get_if<VmSymbol>(&value)) {
      if (item->value == 0)
        throw IrError("IR hierarchy received an invalid symbol");
      return item->value;
    }
    if (const auto fact = std::get_if<VmFactPtr>(&value); fact && *fact)
      return (*fact)->type;
    throw IrError("IR hierarchy operands must be symbols or facts");
  };
  const auto symbolArray = [](const std::vector<IrSymbolRef> &symbols) {
    auto result = std::make_shared<VmArray>();
    result->values.reserve(symbols.size());
    for (const auto value : symbols)
      result->values.push_back(VmSymbol{value});
    return result;
  };
  const auto invoke = [&](IrSymbolRef procedureSymbol,
                          std::vector<VmValue> arguments) {
    const auto found = module.procedures.find(procedureSymbol);
    if (found == module.procedures.end())
      throw IrError("IR call target is not a procedure");
    const auto &procedure = found->second;
    std::vector<IrSymbolRef> parameters;
    parameters.reserve(procedure.positionalParameters.size());
    for (const auto parameter : procedure.positionalParameters)
      parameters.push_back(runtime.resolveSymbol(parameter));
    runtime.enterProcedure(runtime.resolveSymbol(procedureSymbol), parameters,
                           arguments);
    struct Frame {
      VmRuntime &runtime;
      ~Frame() { runtime.leaveProcedure(); }
    } frame{runtime};
    return executeIrProgram(module, procedure.ir, runtime, VmNil{},
                            callDepth + 1, instructionSteps);
  };
  for (std::size_t pc = 0; pc < program.words.size();) {
    if (instructionSteps++ >= maximumInstructionSteps_) {
      throw IrError("IR execution exceeds its instruction-step limit");
    }
    const auto op = static_cast<IrOpcode>(program.words.at(pc));
    std::size_t width = 0;
    switch (op) {
    case IrOpcode::End:
      throw IrError("IR program completed without returning a value");
    case IrOpcode::LoadConst: {
      const auto destination = program.words.at(pc + 1);
      const auto index = program.words.at(pc + 2);
      const auto &constant = program.constants.at(index);
      if (constant.kind == IrConstantKind::Number)
        registers.at(destination) = decodeIrNumber(constant.value);
      else if (constant.kind == IrConstantKind::Boolean)
        registers.at(destination) = constant.value == 0 ? 0.0 : 1.0;
      else if (constant.kind == IrConstantKind::Nil)
        registers.at(destination) = VmNil{};
      else
        registers.at(destination) = VmText{program.texts.at(constant.value)};
      width = 3;
      break;
    }
    case IrOpcode::LoadSymbol:
      registers.at(program.words.at(pc + 1)) =
          runtime.loadSymbol(symbol(program.words.at(pc + 2)));
      width = 3;
      break;
    case IrOpcode::StoreSymbol:
      runtime.storeSymbol(symbol(program.words.at(pc + 1)),
                          registers.at(program.words.at(pc + 2)));
      width = 3;
      break;
    case IrOpcode::Move:
      registers.at(program.words.at(pc + 1)) =
          registers.at(program.words.at(pc + 2));
      width = 3;
      break;
    case IrOpcode::Add:
    case IrOpcode::Sub:
    case IrOpcode::Mul:
    case IrOpcode::Div:
    case IrOpcode::Mod: {
      const auto lhs =
          std::get_if<double>(&registers.at(program.words.at(pc + 2)));
      const auto rhs =
          std::get_if<double>(&registers.at(program.words.at(pc + 3)));
      if (!lhs || !rhs)
        throw IrError("IR arithmetic operands must be numbers");
      double value = 0.0;
      if (op == IrOpcode::Add)
        value = *lhs + *rhs;
      else if (op == IrOpcode::Sub)
        value = *lhs - *rhs;
      else if (op == IrOpcode::Mul)
        value = *lhs * *rhs;
      else if (op == IrOpcode::Div) {
        if (*rhs == 0.0)
          throw IrError("IR division by zero");
        value = *lhs / *rhs;
      } else {
        if (*rhs == 0.0)
          throw IrError("IR modulo by zero");
        value = std::fmod(*lhs, *rhs);
      }
      registers.at(program.words.at(pc + 1)) = value;
      width = 4;
      break;
    }
    case IrOpcode::Compare: {
      const auto &lhs = registers.at(program.words.at(pc + 2));
      const auto &rhs = registers.at(program.words.at(pc + 3));
      const auto comparison =
          static_cast<IrComparison>(program.words.at(pc + 4));
      bool value = false;
      if (comparison == IrComparison::Equal ||
          comparison == IrComparison::NotEqual) {
        value = vmValuesEqual(lhs, rhs);
        if (comparison == IrComparison::NotEqual)
          value = !value;
      } else {
        const auto numeric = [](const VmValue &item) -> std::optional<double> {
          if (const auto number = std::get_if<double>(&item))
            return *number;
          if (const auto degree = std::get_if<VmDegree>(&item))
            return degree->value;
          return std::nullopt;
        };
        const auto left = numeric(lhs), right = numeric(rhs);
        if (!left || !right)
          throw IrError("IR ordered comparison requires numeric operands");
        if (comparison == IrComparison::Less)
          value = *left < *right;
        else if (comparison == IrComparison::LessEqual)
          value = *left <= *right;
        else if (comparison == IrComparison::Greater)
          value = *left > *right;
        else
          value = *left >= *right;
      }
      registers.at(program.words.at(pc + 1)) = value ? 1.0 : 0.0;
      width = 5;
      break;
    }
    case IrOpcode::Jump:
      pc = program.words.at(pc + 1);
      continue;
    case IrOpcode::JumpIfFalse:
      if (runtime.shouldBranchFalse(registers.at(program.words.at(pc + 1)))) {
        pc = program.words.at(pc + 2);
        continue;
      }
      width = 3;
      break;
    case IrOpcode::Call: {
      const auto count = program.words.at(pc + 3);
      std::vector<VmValue> arguments;
      arguments.reserve(count);
      for (IrWord index = 0; index < count; ++index)
        arguments.push_back(registers.at(program.words.at(pc + 4 + index)));
      registers.at(program.words.at(pc + 1)) =
          invoke(moduleSymbol(program.words.at(pc + 2)), std::move(arguments));
      width = 4 + count;
      break;
    }
    case IrOpcode::CallNamed: {
      const auto procedureSymbol = moduleSymbol(program.words.at(pc + 2));
      const auto found = module.procedures.find(procedureSymbol);
      if (found == module.procedures.end())
        throw IrError("IR named-call target is not a procedure");
      const auto &procedure = found->second;
      std::vector<std::optional<VmValue>> mapped(
          procedure.positionalParameters.size());
      std::size_t next = 0;
      const auto count = program.words.at(pc + 3);
      for (IrWord index = 0; index < count; ++index) {
        const auto encodedName = program.words.at(pc + 4 + 2 * index);
        std::size_t target = 0;
        if (encodedName == 0) {
          while (next < mapped.size() && mapped[next])
            ++next;
          if (next == mapped.size())
            throw IrError("IR named call has too many positional arguments");
          target = next++;
        } else {
          const auto name = moduleSymbol(encodedName - 1);
          const auto parameter =
              std::find(procedure.namedParameters.begin(),
                        procedure.namedParameters.end(), name);
          if (parameter == procedure.namedParameters.end())
            throw IrError("IR named call has an unknown argument");
          target = static_cast<std::size_t>(parameter -
                                            procedure.namedParameters.begin());
        }
        if (mapped[target])
          throw IrError("IR named call duplicates an argument");
        mapped[target] = registers.at(program.words.at(pc + 5 + 2 * index));
      }
      std::vector<VmValue> arguments;
      arguments.reserve(mapped.size());
      for (auto &value : mapped) {
        if (!value)
          throw IrError("IR named call omits an argument");
        arguments.push_back(std::move(*value));
      }
      registers.at(program.words.at(pc + 1)) =
          invoke(procedureSymbol, std::move(arguments));
      width = 4 + 2 * count;
      break;
    }
    case IrOpcode::CallNative:
      registers.at(program.words.at(pc + 1)) =
          runtime.callNativeSymbol(symbol(program.words.at(pc + 2)));
      width = 3;
      break;
    case IrOpcode::SemanticEval: {
      const auto operation =
          static_cast<std::uint16_t>(program.words.at(pc + 2));
      const auto count = program.words.at(pc + 3);
      std::vector<VmValue> inputs;
      inputs.reserve(count);
      for (IrWord index = 0; index < count; ++index)
        inputs.push_back(registers.at(program.words.at(pc + 4 + index)));
      auto *model = runtime.runtimeStateModel();
      if (!model)
        throw IrError("IR SemanticEval requires a RuntimeStateModel");
      runtime.refreshRuntimeContext(semanticContext);
      const auto steps = semanticContext.sharedSemanticSteps
                             ? *semanticContext.sharedSemanticSteps
                             : semanticContext.semanticSteps;
      if (steps >= semanticContext.maximumSemanticSteps)
        throw IrError("IR semantic operation exceeds execution state limit");
      if (!validSemanticInputs(operation, inputs))
        throw IrError("IR SemanticEval input contract is invalid");
      if (semanticContext.sharedSemanticSteps)
        semanticContext.semanticSteps = ++*semanticContext.sharedSemanticSteps;
      else
        ++semanticContext.semanticSteps;
      auto value =
          model->evaluate(RuntimeOperation{operation}, inputs, semanticContext);
      if (!validSemanticOutput(operation, inputs, value)) {
        throw IrError("RuntimeStateModel returned an invalid typed result");
      }
      if (const auto fact = std::get_if<VmFactPtr>(&value); fact && *fact) {
        (*fact)->origin = VmFact::Origin::Derived;
        runtime.retainFact(*fact);
      }
      registers.at(program.words.at(pc + 1)) = std::move(value);
      width = 4 + count;
      break;
    }
    case IrOpcode::Numeric: {
      const auto count = program.words.at(pc + 3);
      std::vector<double> operands;
      operands.reserve(count);
      for (std::size_t index = 0; index < count; ++index) {
        const auto *number = std::get_if<double>(
            &registers.at(program.words.at(pc + 4 + index)));
        if (!number)
          throw IrError("numeric operation operands must be numbers");
        operands.push_back(*number);
      }
      registers.at(program.words.at(pc + 1)) = evaluateNumericOperation(
          static_cast<NumericOperation>(program.words.at(pc + 2)), operands);
      width = 4 + count;
      break;
    }
    case IrOpcode::MakeFact: {
      auto fact = std::make_shared<VmFact>();
      fact->type = symbol(program.words.at(pc + 2));
      runtime.retainFact(fact);
      registers.at(program.words.at(pc + 1)) = std::move(fact);
      width = 3;
      break;
    }
    case IrOpcode::MakeArray: {
      auto array = std::make_shared<VmArray>();
      const auto count = program.words.at(pc + 3);
      array->values.reserve(count);
      for (IrWord index = 0; index < count; ++index)
        array->values.push_back(registers.at(program.words.at(pc + 4 + index)));
      registers.at(program.words.at(pc + 1)) = std::move(array);
      width = 4 + count;
      break;
    }
    case IrOpcode::MakeMap: {
      auto map = std::make_shared<VmMap>();
      const auto count = program.words.at(pc + 3);
      map->entries.reserve(count);
      for (IrWord index = 0; index < count; ++index) {
        map->entries.emplace_back(
            symbol(program.words.at(pc + 4 + 2 * index)),
            registers.at(program.words.at(pc + 5 + 2 * index)));
      }
      registers.at(program.words.at(pc + 1)) = std::move(map);
      width = 4 + 2 * count;
      break;
    }
    case IrOpcode::GetField: {
      const auto field = symbol(program.words.at(pc + 3));
      const auto read = [&](const auto &entries) -> VmValue {
        for (const auto &[key, value] : entries)
          if (key == field)
            return value;
        throw IrError("IR field is absent");
      };
      const auto target = program.words.at(pc + 2);
      if (const auto fact = std::get_if<VmFactPtr>(&registers.at(target));
          fact && *fact)
        registers.at(program.words.at(pc + 1)) = read((*fact)->fields);
      else if (const auto map = std::get_if<VmMapPtr>(&registers.at(target));
               map && *map)
        registers.at(program.words.at(pc + 1)) = read((*map)->entries);
      else
        throw IrError("IR field access requires a fact or map");
      width = 4;
      break;
    }
    case IrOpcode::SetField: {
      const auto field = symbol(program.words.at(pc + 2));
      const auto value = registers.at(program.words.at(pc + 3));
      const auto object = program.words.at(pc + 1);
      if (const auto fact = std::get_if<VmFactPtr>(&registers.at(object));
          fact && *fact)
        runtime.mutateFact(*fact, field, value);
      else if (const auto map = std::get_if<VmMapPtr>(&registers.at(object));
               map && *map) {
        const auto found = std::find_if(
            (*map)->entries.begin(), (*map)->entries.end(),
            [&](const auto &entry) { return entry.first == field; });
        if (found == (*map)->entries.end())
          (*map)->entries.emplace_back(field, value);
        else
          found->second = value;
      } else
        throw IrError("IR field assignment requires a fact or map");
      width = 4;
      break;
    }
    case IrOpcode::Similarity:
      registers.at(program.words.at(pc + 1)) =
          VmDegree(similarityDegree(registers.at(program.words.at(pc + 2)),
                                    registers.at(program.words.at(pc + 3))));
      width = 4;
      break;
    case IrOpcode::Membership: {
      const auto number = [&](std::size_t offset) {
        const auto value =
            std::get_if<double>(&registers.at(program.words.at(pc + offset)));
        if (!value)
          throw IrError("IR membership operands must be numbers");
        return *value;
      };
      registers.at(program.words.at(pc + 1)) =
          VmDegree(VmFactStore::gaussianMembership(
              number(2), {number(3), number(4), number(5)}));
      width = 6;
      break;
    }
    case IrOpcode::ForEachFact: {
      const auto facts =
          runtime.snapshotFacts(symbol(program.words.at(pc + 2)));
      auto values = std::make_shared<VmArray>();
      values->values.reserve(facts.size());
      for (const auto &fact : facts)
        values->values.push_back(
            invoke(moduleSymbol(program.words.at(pc + 3)), {VmValue{fact}}));
      registers.at(program.words.at(pc + 1)) = std::move(values);
      width = 4;
      break;
    }
    case IrOpcode::HierarchyIsA:
      registers.at(program.words.at(pc + 1)) =
          runtime.hierarchyProof(
                     typeSymbol(registers.at(program.words.at(pc + 2))),
                     typeSymbol(registers.at(program.words.at(pc + 3))))
                  .empty()
              ? 0.0
              : 1.0;
      width = 4;
      break;
    case IrOpcode::HierarchyCommonAncestors:
      registers.at(program.words.at(pc + 1)) =
          symbolArray(runtime.commonAncestors(
              typeSymbol(registers.at(program.words.at(pc + 2))),
              typeSymbol(registers.at(program.words.at(pc + 3)))));
      width = 4;
      break;
    case IrOpcode::HierarchyLeastCommonAncestors:
      registers.at(program.words.at(pc + 1)) =
          symbolArray(runtime.leastCommonAncestors(
              typeSymbol(registers.at(program.words.at(pc + 2))),
              typeSymbol(registers.at(program.words.at(pc + 3)))));
      width = 4;
      break;
    case IrOpcode::HierarchyMostGeneralAncestors:
      registers.at(program.words.at(pc + 1)) =
          symbolArray(runtime.mostGeneralCommonAncestors(
              typeSymbol(registers.at(program.words.at(pc + 2))),
              typeSymbol(registers.at(program.words.at(pc + 3)))));
      width = 4;
      break;
    case IrOpcode::TemporalRank: {
      const auto ranked = runtime.rankFacts(symbol(program.words.at(pc + 2)),
                                            symbol(program.words.at(pc + 3)));
      auto values = std::make_shared<VmArray>();
      values->values.reserve(ranked.size());
      for (const auto &item : ranked)
        values->values.push_back(item.fact);
      registers.at(program.words.at(pc + 1)) = std::move(values);
      width = 4;
      break;
    }
    case IrOpcode::Return:
      return registers.at(program.words.at(pc + 1));
    case IrOpcode::Count:
      throw IrError("IR contains an invalid opcode");
    }
    pc += width;
  }
  throw IrError("IR program ended without Return or End");
}
} // namespace Felidae
