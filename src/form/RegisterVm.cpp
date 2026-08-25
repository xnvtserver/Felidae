#include "RegisterVm.h"

#ifdef FELIDAE_IR_VERIFIER_ONLY
#include "IrModule.h"
#endif
#include "FelidaeIsa.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace Felidae {

#ifndef FELIDAE_IR_VERIFIER_ONLY
RuntimeValueKind runtimeValueKind(const VmValue& value) noexcept {
    if (std::holds_alternative<VmNil>(value)) return RuntimeValueKind::Nil;
    if (std::holds_alternative<double>(value)) return RuntimeValueKind::Number;
    if (std::holds_alternative<VmDegree>(value)) return RuntimeValueKind::Degree;
    if (std::holds_alternative<VmText>(value)) return RuntimeValueKind::Text;
    if (std::holds_alternative<VmSymbol>(value)) return RuntimeValueKind::Symbol;
    if (std::holds_alternative<VmArrayPtr>(value)) return RuntimeValueKind::Array;
    if (std::holds_alternative<VmMapPtr>(value)) return RuntimeValueKind::Map;
    return RuntimeValueKind::Fact;
}
VmDegree::VmDegree(double value) : value(value) {
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) throw IrError("Degree must be finite and within [0,1]");
}
#endif
namespace {

#ifndef NDEBUG
bool vmTraceEnabled() {
    static const bool enabled = [] {
        const auto* value = std::getenv("FELIDAE_TRACE");
        return value != nullptr && *value != '\0' && std::string_view(value) != "0";
    }();
    return enabled;
}
#endif

#ifdef FELIDAE_IR_VERIFIER_ONLY
constexpr std::size_t kMaximumIrWords = 1'000'000;
constexpr std::size_t kMaximumRegisters = 65'536;
constexpr std::size_t kMaximumIrTableEntries = 1'000'000;
constexpr std::size_t kMaximumTextBytes = 16ull * 1024ull * 1024ull;

IrOpcode opcodeAt(const FelidaeIr& ir, std::size_t index) {
    if (ir.words[index] >= kIrOpcodeCount) {
        throw IrError("IR contains an invalid opcode");
    }
    return static_cast<IrOpcode>(ir.words[index]);
}

void requireWords(const FelidaeIr& ir, std::size_t pc, std::size_t width) {
    if (width > ir.words.size() - pc) throw IrError("IR instruction is truncated");
}

std::size_t dynamicWidth(const FelidaeIr& ir, std::size_t pc, std::size_t stride) {
    requireWords(ir, pc, 4);
    const auto count = ir.words[pc + 3];
    if (count > (std::numeric_limits<std::size_t>::max() - 4) / stride) {
        throw IrError("IR dynamic operand count overflows instruction width");
    }
    const auto width = 4 + count * stride;
    requireWords(ir, pc, width);
    return width;
}

void requireRegister(const FelidaeIr& ir, IrWord word) {
    if (word >= ir.registerCount) throw IrError("IR references an invalid register");
}

void requireInitialized(const std::vector<bool>& initialized, IrWord word) {
    if (!initialized[word]) throw IrError("IR reads an uninitialized register");
}

std::size_t widthFor(IrOpcode op) {
    switch (op) {
    case IrOpcode::End: return 1;
    case IrOpcode::Jump: return 2;
    case IrOpcode::LoadSymbol: case IrOpcode::StoreSymbol: case IrOpcode::LoadConst: case IrOpcode::Move:
    case IrOpcode::JumpIfFalse: case IrOpcode::CallNative:
    case IrOpcode::MakeFact: case IrOpcode::Return: return 3;
    case IrOpcode::ForEachFact: return 4;
    case IrOpcode::Call: case IrOpcode::CallNamed: case IrOpcode::SemanticEval:
        return 0; // validated below from explicit argument count
    case IrOpcode::MakeArray: return 0; // destination, explicit item count, registers
    case IrOpcode::MakeMap: return 0; // destination, explicit entry count, symbol/register pairs
    case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div: case IrOpcode::Mod:
    case IrOpcode::GetField: case IrOpcode::SetField: case IrOpcode::Similarity:
    case IrOpcode::HierarchyIsA:
    case IrOpcode::HierarchyCommonAncestors:
    case IrOpcode::HierarchyLeastCommonAncestors:
    case IrOpcode::HierarchyMostGeneralAncestors: case IrOpcode::TemporalRank: return 4;
    case IrOpcode::Membership: return 6;
    case IrOpcode::Compare: return 5;
    case IrOpcode::Count: break;
    }
    throw IrError("IR contains an invalid opcode");
}

void requireFlowRead(const std::vector<bool>& initialized, IrWord registerId) {
    if (!initialized[registerId]) throw IrError("IR control-flow path reads an uninitialized register");
}
#else
bool vmValuesEqual(const VmValue& left, const VmValue& right) {
    if (left.index() != right.index()) return false;
    if (std::holds_alternative<VmNil>(left)) return true;
    if (const auto* value = std::get_if<double>(&left)) return *value == std::get<double>(right);
    if (const auto* value = std::get_if<VmDegree>(&left)) return *value == std::get<VmDegree>(right);
    if (const auto* value = std::get_if<VmText>(&left)) return *value == std::get<VmText>(right);
    if (const auto* value = std::get_if<VmSymbol>(&left)) return *value == std::get<VmSymbol>(right);
    if (const auto* leftArray = std::get_if<VmArrayPtr>(&left)) {
        const auto& rightArray = std::get<VmArrayPtr>(right);
        if (!*leftArray || !rightArray) return *leftArray == rightArray;
        if ((*leftArray)->values.size() != rightArray->values.size()) return false;
        for (std::size_t index = 0; index < (*leftArray)->values.size(); ++index) {
            if (!vmValuesEqual((*leftArray)->values[index], rightArray->values[index])) return false;
        }
        return true;
    }
    if (const auto* leftMap = std::get_if<VmMapPtr>(&left)) {
        const auto& rightMap = std::get<VmMapPtr>(right);
        if (!*leftMap || !rightMap) return *leftMap == rightMap;
        if ((*leftMap)->entries.size() != rightMap->entries.size()) return false;
        for (std::size_t index = 0; index < (*leftMap)->entries.size(); ++index) {
            const auto& [leftKey, leftValue] = (*leftMap)->entries[index];
            const auto& [rightKey, rightValue] = rightMap->entries[index];
            if (leftKey != rightKey || !vmValuesEqual(leftValue, rightValue)) return false;
        }
        return true;
    }
    if (const auto* leftFact = std::get_if<VmFactPtr>(&left)) {
        const auto& rightFact = std::get<VmFactPtr>(right);
        if (!*leftFact || !rightFact) return *leftFact == rightFact;
        if ((*leftFact)->type != rightFact->type || (*leftFact)->fields.size() != rightFact->fields.size()) return false;
        for (std::size_t index = 0; index < (*leftFact)->fields.size(); ++index) {
            const auto& [leftKey, leftValue] = (*leftFact)->fields[index];
            const auto& [rightKey, rightValue] = rightFact->fields[index];
            if (leftKey != rightKey || !vmValuesEqual(leftValue, rightValue)) return false;
        }
        return true;
    }
    throw IrError("VM equality received an unsupported value variant");
}

double similarityDegree(const VmValue& left, const VmValue& right) {
    if (const auto a = std::get_if<double>(&left)) {
        if (const auto b = std::get_if<double>(&right)) return 1.0 / (1.0 + std::abs(*a - *b));
    }
    if (const auto a = std::get_if<VmDegree>(&left)) {
        if (const auto b = std::get_if<VmDegree>(&right)) return 1.0 - std::abs(a->value - b->value);
    }
    if (const auto a = std::get_if<VmText>(&left)) {
        if (const auto b = std::get_if<VmText>(&right)) return a->value == b->value ? 1.0 : 0.0;
    }
    if (const auto a = std::get_if<VmSymbol>(&left)) {
        if (const auto b = std::get_if<VmSymbol>(&right)) return a->value == b->value ? 1.0 : 0.0;
    }
    if (const auto a = std::get_if<VmArrayPtr>(&left)) {
        if (const auto b = std::get_if<VmArrayPtr>(&right)) {
            if (!*a || !*b) return *a == *b ? 1.0 : 0.0;
            const auto limit = std::max((*a)->values.size(), (*b)->values.size());
            if (limit == 0) return 1.0;
            double sum = 0.0;
            for (std::size_t i = 0; i < std::min((*a)->values.size(), (*b)->values.size()); ++i) sum += similarityDegree((*a)->values[i], (*b)->values[i]);
            return sum / static_cast<double>(limit);
        }
    }
    if (const auto a = std::get_if<VmMapPtr>(&left)) {
        if (const auto b = std::get_if<VmMapPtr>(&right)) {
            if (!*a || !*b) return *a == *b ? 1.0 : 0.0;
            const auto limit = std::max((*a)->entries.size(), (*b)->entries.size());
            if (limit == 0) return 1.0;
            double sum = 0.0;
            for (const auto& [key, value] : (*a)->entries) for (const auto& [otherKey, otherValue] : (*b)->entries) if (key == otherKey) { sum += similarityDegree(value, otherValue); break; }
            return sum / static_cast<double>(limit);
        }
    }
    if (const auto a = std::get_if<VmFactPtr>(&left)) {
        if (const auto b = std::get_if<VmFactPtr>(&right)) {
            if (!*a || !*b) return *a == *b ? 1.0 : 0.0;
            const double type = (*a)->type == (*b)->type ? 1.0 : 0.0;
            VmMap leftMap{(*a)->fields}; VmMap rightMap{(*b)->fields};
            return 0.25 * type + 0.75 * similarityDegree(VmMapPtr(&leftMap, [](VmMap*){}), VmMapPtr(&rightMap, [](VmMap*){}));
        }
    }
    return vmValuesEqual(left, right) ? 1.0 : 0.0;
}

bool validVmValue(const VmValue& value, std::size_t depth = 0) {
    constexpr std::size_t kMaximumValueDepth = 256;
    if (depth > kMaximumValueDepth) return false;
    if (const auto number = std::get_if<double>(&value)) return std::isfinite(*number);
    if (const auto degree = std::get_if<VmDegree>(&value)) return std::isfinite(degree->value) && degree->value >= 0.0 && degree->value <= 1.0;
    if (const auto* array = std::get_if<VmArrayPtr>(&value)) {
        if (!*array) return false;
        return std::all_of((*array)->values.begin(), (*array)->values.end(),
            [&](const VmValue& item) { return validVmValue(item, depth + 1); });
    }
    if (const auto* map = std::get_if<VmMapPtr>(&value)) {
        if (!*map) return false;
        return std::all_of((*map)->entries.begin(), (*map)->entries.end(),
            [&](const auto& entry) { return validVmValue(entry.second, depth + 1); });
    }
    if (const auto* fact = std::get_if<VmFactPtr>(&value)) {
        if (!*fact) return false;
        return std::all_of((*fact)->fields.begin(), (*fact)->fields.end(),
            [&](const auto& entry) { return validVmValue(entry.second, depth + 1); });
    }
    return true;
}

bool validSemanticInputs(std::uint16_t operation,
                         std::span<const VmValue> inputs) {
    if (!semanticOperationAcceptsArity(operation, inputs.size()) ||
        !validVmValue(inputs.front())) return false;
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
                         const VmValue& output) {
    if (!validVmValue(output)) return false;
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
#endif

#ifdef FELIDAE_IR_VERIFIER_ONLY
void verifyControlFlowInitialization(const FelidaeIr& ir,
                                     const std::unordered_set<std::size_t>& boundaries) {
    std::vector<std::optional<std::vector<bool>>> incoming(ir.words.size());
    std::deque<std::size_t> pending;
    incoming[0] = std::vector<bool>(ir.registerCount, false);
    pending.push_back(0);
    const auto enqueue = [&](std::size_t target, const std::vector<bool>& state,
                             std::deque<std::size_t>& queue,
                             std::vector<std::optional<std::vector<bool>>>& states) {
        if (!boundaries.contains(target)) return;
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
        const auto next = pc + (op == IrOpcode::Call || op == IrOpcode::SemanticEval || op == IrOpcode::MakeArray
            ? dynamicWidth(ir, pc, 1)
            : op == IrOpcode::CallNamed || op == IrOpcode::MakeMap ? dynamicWidth(ir, pc, 2) : widthFor(op));
        auto write = [&](IrWord target) { state[target] = true; };
        switch (op) {
        case IrOpcode::LoadConst: case IrOpcode::LoadSymbol: case IrOpcode::ForEachFact:
        case IrOpcode::TemporalRank: write(ir.words[pc + 1]); break;
        case IrOpcode::StoreSymbol: requireFlowRead(state, ir.words[pc + 2]); break;
        case IrOpcode::Move: requireFlowRead(state, ir.words[pc + 2]); write(ir.words[pc + 1]); break;
        case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div: case IrOpcode::Mod:
        case IrOpcode::Similarity: case IrOpcode::Membership:
        case IrOpcode::HierarchyIsA:
        case IrOpcode::HierarchyCommonAncestors:
        case IrOpcode::HierarchyLeastCommonAncestors:
        case IrOpcode::HierarchyMostGeneralAncestors:
        case IrOpcode::Compare:
            requireFlowRead(state, ir.words[pc + 2]); requireFlowRead(state, ir.words[pc + 3]); write(ir.words[pc + 1]); break;
        case IrOpcode::JumpIfFalse: requireFlowRead(state, ir.words[pc + 1]); break;
        case IrOpcode::Call:
            for (std::size_t i = 0; i < ir.words[pc + 3]; ++i) requireFlowRead(state, ir.words[pc + 4 + i]);
            write(ir.words[pc + 1]); break;
        case IrOpcode::SemanticEval:
            for (std::size_t i = 0; i < ir.words[pc + 3]; ++i) requireFlowRead(state, ir.words[pc + 4 + i]);
            write(ir.words[pc + 1]); break;
        case IrOpcode::CallNamed:
            for (std::size_t i = 0; i < ir.words[pc + 3]; ++i) requireFlowRead(state, ir.words[pc + 5 + 2 * i]);
            write(ir.words[pc + 1]); break;
        case IrOpcode::MakeArray:
            for (std::size_t i = 0; i < ir.words[pc + 3]; ++i) requireFlowRead(state, ir.words[pc + 4 + i]);
            write(ir.words[pc + 1]); break;
        case IrOpcode::MakeMap:
            for (std::size_t i = 0; i < ir.words[pc + 3]; ++i) requireFlowRead(state, ir.words[pc + 5 + 2 * i]);
            write(ir.words[pc + 1]); break;
        case IrOpcode::GetField: requireFlowRead(state, ir.words[pc + 2]); write(ir.words[pc + 1]); break;
        case IrOpcode::SetField: requireFlowRead(state, ir.words[pc + 1]); requireFlowRead(state, ir.words[pc + 3]); break;
        case IrOpcode::Return: requireFlowRead(state, ir.words[pc + 1]); break;
        case IrOpcode::CallNative: case IrOpcode::MakeFact: write(ir.words[pc + 1]); break;
        case IrOpcode::End: case IrOpcode::Jump: break;
        case IrOpcode::Count: throw IrError("IR control-flow scan found an invalid opcode");
        }
        if (op == IrOpcode::End || op == IrOpcode::Return) continue;
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
#endif

} // namespace

#ifndef FELIDAE_IR_VERIFIER_ONLY
std::string vmValueToDisplayString(const VmValue& value, const VmDisplayContext& context) {
    const auto render = [&](const auto& self, const VmValue& item) -> std::string {
        if (std::holds_alternative<VmNil>(item)) return "nil";
        if (const auto number = std::get_if<double>(&item)) {
            std::ostringstream out;
            out.precision(15);
            out << *number;
            return out.str();
        }
        if (const auto degree = std::get_if<VmDegree>(&item)) {
            std::ostringstream out;
            out.precision(15);
            out << degree->value;
            return out.str();
        }
        if (const auto text = std::get_if<VmText>(&item)) return text->value;
        if (const auto symbol = std::get_if<VmSymbol>(&item)) {
            auto name=context.symbolDecoder?context.symbolDecoder(symbol->value):std::string{};
            return name.empty()?"#"+std::to_string(symbol->value):name;
        }
        if (const auto array = std::get_if<VmArrayPtr>(&item)) {
            if (!*array) throw IrError("VM display received an invalid array value");
            std::ostringstream out;
            out << "[";
            for (std::size_t index = 0; index < (*array)->values.size(); ++index) {
                if (index) out << ", ";
                out << self(self, (*array)->values[index]);
            }
            return out << "]", out.str();
        }
        const auto renderFields = [&](const auto& fields) {
            std::ostringstream out;
            out << "{";
            for (std::size_t index = 0; index < fields.size(); ++index) {
                if (index) out << ", ";
                auto name = context.symbolDecoder ? context.symbolDecoder(fields[index].first) : std::string{};
                if (name.empty()) name = "#" + std::to_string(fields[index].first);
                out << name << ": "
                    << self(self, fields[index].second);
            }
            return out << "}", out.str();
        };
        if (const auto map = std::get_if<VmMapPtr>(&item)) {
            if (!*map) throw IrError("VM display received an invalid map value");
            return renderFields((*map)->entries);
        }
        if (const auto fact = std::get_if<VmFactPtr>(&item)) {
            if (!*fact) throw IrError("VM display received an invalid fact value");
            return renderFields((*fact)->fields);
        }
        throw IrError("Direct VM result cannot display an opaque runtime value");
    };
    return render(render, value);
}

void VmFactStore::registerType(IrSymbolRef type, std::vector<IrSymbolRef> parents) {
    if (type == 0) throw IrError("fact hierarchy type is invalid");
    std::lock_guard lock(mutex_);
    if (const auto existing = parents_.find(type); existing != parents_.end()) {
        if (existing->second != parents) throw IrError("fact hierarchy type has conflicting parents");
        return;
    }
    for (const auto parent : parents) {
        if (parent == 0 || parent == type) throw IrError("fact hierarchy parent is invalid");
    }
    parents_.emplace(type, std::move(parents));
    ++revision_;
}

IrFactRef VmFactStore::retain(const VmFactPtr& fact) {
    if (!fact) throw IrError("fact store cannot retain a null fact");
    std::lock_guard lock(mutex_);
    if (fact->id == 0) {
        if (nextId_ == std::numeric_limits<IrFactRef>::max()) {
            throw IrError("fact store exhausted its fact ID space");
        }
        fact->id = nextId_++;
        fact->createdSequence = nextSequence_++;
        facts_.push_back(fact);
        byType_[fact->type].push_back(fact);
        for (const auto& [field, _] : fact->fields) byField_[field].push_back(fact);
        provenance_.push_back(VmFactProvenance{fact->id, 0, fact->origin == VmFact::Origin::Derived});
        ++revision_;
    }
    return fact->id;
}

void VmFactStore::mutate(const VmFactPtr& fact, IrSymbolRef field, const VmValue& value,
                         IrSymbolRef procedure) {
    if (!fact || fact->id == 0 || field == 0) throw IrError("fact mutation is invalid");
    std::lock_guard lock(mutex_);
    const auto known = std::find_if(facts_.begin(), facts_.end(), [&](const auto& item) { return item == fact; });
    if (known == facts_.end()) throw IrError("fact mutation targets a fact outside this knowledge runtime");
    bool hadField = false;
    for (auto& [existing, previous] : fact->fields) {
        if (existing == field) { previous = value; hadField = true; break; }
    }
    if (!hadField) {
        fact->fields.emplace_back(field, value);
        byField_[field].push_back(fact);
    }
    mutations_.push_back(VmFactMutation{nextSequence_++, fact->id, field});
    provenance_.push_back(VmFactProvenance{fact->id, procedure, fact->origin == VmFact::Origin::Derived});
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

std::vector<IrSymbolRef> VmFactStore::hierarchyProof(IrSymbolRef child, IrSymbolRef ancestor) const {
    std::lock_guard lock(mutex_);
    if (child == 0 || ancestor == 0) return {};
    std::vector<std::pair<IrSymbolRef, std::vector<IrSymbolRef>>> pending{{child, {child}}};
    std::unordered_set<IrSymbolRef> visited;
    while (!pending.empty()) {
        auto [current, proof] = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(current).second) continue;
        if (current == ancestor) return proof;
        const auto found = parents_.find(current);
        if (found == parents_.end()) continue;
        for (const auto parent : found->second) {
            auto next = proof;
            next.push_back(parent);
            pending.emplace_back(parent, std::move(next));
        }
    }
    return {};
}

std::vector<IrSymbolRef> VmFactStore::commonAncestors(IrSymbolRef left, IrSymbolRef right) const {
    std::lock_guard lock(mutex_);
    if (left == 0 || right == 0) return {};
    const auto closure = [&](IrSymbolRef type) {
        std::unordered_set<IrSymbolRef> result;
        std::vector<IrSymbolRef> pending{type};
        while (!pending.empty()) {
            const auto current = pending.back(); pending.pop_back();
            if (!result.insert(current).second) continue;
            if (const auto found = parents_.find(current); found != parents_.end()) {
                pending.insert(pending.end(), found->second.begin(), found->second.end());
            }
        }
        return result;
    };
    const auto leftAncestors = closure(left);
    const auto rightAncestors = closure(right);
    std::vector<IrSymbolRef> result;
    for (const auto candidate : leftAncestors) if (rightAncestors.contains(candidate)) result.push_back(candidate);
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<IrSymbolRef> VmFactStore::leastCommonAncestors(IrSymbolRef left, IrSymbolRef right) const {
    const auto common = commonAncestors(left, right);
    std::vector<IrSymbolRef> result;
    for (const auto candidate : common) {
        bool hasMoreSpecificCommon = false;
        for (const auto other : common) {
            if (candidate == other) continue;
            if (!hierarchyProof(other, candidate).empty()) { hasMoreSpecificCommon = true; break; }
        }
        if (!hasMoreSpecificCommon) result.push_back(candidate);
    }
    return result;
}

std::vector<IrSymbolRef> VmFactStore::mostGeneralCommonAncestors(IrSymbolRef left, IrSymbolRef right) const {
    const auto common = commonAncestors(left, right);
    std::vector<IrSymbolRef> result;
    for (const auto candidate : common) {
        bool hasMoreGeneralCommon = false;
        for (const auto other : common) {
            if (candidate == other) continue;
            if (!hierarchyProof(candidate, other).empty()) { hasMoreGeneralCommon = true; break; }
        }
        if (!hasMoreGeneralCommon) result.push_back(candidate);
    }
    return result;
}

std::vector<VmRankedFact> VmFactStore::rankByTimeAndPriority(IrSymbolRef effectiveAtField,
                                                              IrSymbolRef priorityField) const {
    if (effectiveAtField == 0 || priorityField == 0) throw IrError("fact ranking fields are invalid");
    std::lock_guard lock(mutex_);
    const auto numberField = [](const VmFactPtr& fact, IrSymbolRef field, const char* label) {
        const auto found = std::find_if(fact->fields.begin(), fact->fields.end(), [&](const auto& item) { return item.first == field; });
        if (found == fact->fields.end()) throw IrError(std::string("fact has no ") + label + " field");
        const auto number = std::get_if<double>(&found->second);
        if (!number || !std::isfinite(*number)) throw IrError(std::string("fact ") + label + " field is not finite numeric data");
        return *number;
    };
    std::vector<VmRankedFact> ranked;
    ranked.reserve(facts_.size());
    for (const auto& fact : facts_) ranked.push_back({fact, numberField(fact, effectiveAtField, "effective-at"), numberField(fact, priorityField, "priority")});
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        if (left.effectiveAt != right.effectiveAt) return left.effectiveAt > right.effectiveAt;
        if (left.priority != right.priority) return left.priority > right.priority;
        return left.fact->id < right.fact->id;
    });
    return ranked;
}

std::vector<VmFactPtr> VmFactStore::snapshotAssignableTo(IrSymbolRef type) const {
    std::lock_guard lock(mutex_);
    if (const auto cached = assignableCache_.find(type); cached != assignableCache_.end() && cached->second.first == revision_) {
        return cached->second.second;
    }
    std::vector<VmFactPtr> result;
    for (const auto& [candidate, facts] : byType_) {
        bool matches = candidate == type;
        if (!matches) {
            std::vector<IrSymbolRef> pending{candidate};
            std::unordered_set<IrSymbolRef> visited;
            while (!pending.empty() && !matches) {
                const auto current = pending.back(); pending.pop_back();
                if (!visited.insert(current).second) continue;
                const auto found = parents_.find(current);
                if (found == parents_.end()) continue;
                for (const auto parent : found->second) {
                    if (parent == type) { matches = true; break; }
                    pending.push_back(parent);
                }
            }
        }
        if (matches) result.insert(result.end(), facts.begin(), facts.end());
    }
    assignableCache_[type] = {revision_, result};
    return result;
}

std::vector<VmFactPtr> VmFactStore::snapshotByField(IrSymbolRef field) const {
    std::lock_guard lock(mutex_);
    const auto found = byField_.find(field);
    return found == byField_.end() ? std::vector<VmFactPtr>{} : found->second;
}

std::vector<VmFactMutation> VmFactStore::mutations() const { std::lock_guard lock(mutex_); return mutations_; }
std::vector<VmFactProvenance> VmFactStore::provenance() const { std::lock_guard lock(mutex_); return provenance_; }

VmKnowledgeSnapshot VmFactStore::knowledgeSnapshot() const {
    std::lock_guard lock(mutex_);
    VmKnowledgeSnapshot result;
    result.factTypes.reserve(byType_.size());
    result.factTypeCounts.reserve(byType_.size());
    for (const auto& [type, facts] : byType_) {
        result.factTypes.push_back(type);
        result.factTypeCounts.emplace_back(type, static_cast<std::uint32_t>(
            std::min(facts.size(), static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))));
    }
    std::sort(result.factTypes.begin(), result.factTypes.end());
    std::sort(result.factTypeCounts.begin(), result.factTypeCounts.end());
    for (const auto& [child, parents] : parents_) {
        for (const auto parent : parents) result.hierarchyEdges.emplace_back(child, parent);
    }
    std::sort(result.hierarchyEdges.begin(), result.hierarchyEdges.end());
    return result;
}

std::size_t VmFactStore::size() const {
    std::lock_guard lock(mutex_);
    return facts_.size();
}

double VmFactStore::gaussianMembership(double value, const VmGaussianProfile& profile) {
    constexpr double kTailMembership = 0.01;
    if (!std::isfinite(value) || !std::isfinite(profile.peak) || !std::isfinite(profile.fadesIn) ||
        !std::isfinite(profile.fadesOut) || profile.fadesIn > profile.peak || profile.peak > profile.fadesOut) {
        throw IrError("Gaussian profile is invalid");
    }
    const auto sigmaFor = [](double distance) { return distance / std::sqrt(-2.0 * std::log(kTailMembership)); };
    const auto leftDistance = profile.peak - profile.fadesIn;
    const auto rightDistance = profile.fadesOut - profile.peak;
    const auto fallback = std::max(leftDistance, rightDistance);
    if (fallback == 0.0) return value == profile.peak ? 1.0 : 0.0;
    const auto sigma = value <= profile.peak
        ? sigmaFor(leftDistance == 0.0 ? fallback : leftDistance)
        : sigmaFor(rightDistance == 0.0 ? fallback : rightDistance);
    const auto z = (value - profile.peak) / sigma;
    return std::exp(-0.5 * z * z);
}

FelidaeKnowledgeRuntime::FelidaeKnowledgeRuntime(RuntimeStateModel* semanticModel,
                                 std::size_t maximumSemanticSteps,
                                 std::size_t maximumCallDepth,
                                 std::shared_ptr<VmFactStore> factStore)
    : factStore_(factStore ? std::move(factStore) : std::make_shared<VmFactStore>()),
      semanticModel_(semanticModel),
      maximumSemanticSteps_(maximumSemanticSteps), maximumCallDepth_(maximumCallDepth) {
    if (maximumSemanticSteps_ == 0) throw IrError("direct VM semantic step limit must be positive");
    if (maximumCallDepth_ == 0) throw IrError("direct VM call depth limit must be positive");
}

void FelidaeKnowledgeRuntime::retainFact(const VmFactPtr& fact) {
    const auto id = factStore_->retain(fact);
    recordTrace(VmTraceKind::FactRetained, fact ? fact->type : 0, id);
}

void FelidaeKnowledgeRuntime::mutateFact(const VmFactPtr& fact, IrSymbolRef field, const VmValue& value) {
    factStore_->mutate(fact, field, value, callFrames_.empty() ? 0 : callFrames_.back().procedure);
}

void FelidaeKnowledgeRuntime::registerFactType(IrSymbolRef type, std::vector<IrSymbolRef> parents) {
    factStore_->registerType(type, std::move(parents));
}

std::vector<VmFactPtr> FelidaeKnowledgeRuntime::snapshotFacts(IrSymbolRef type) {
    return factStore_->snapshotAssignableTo(type);
}

std::vector<IrSymbolRef> FelidaeKnowledgeRuntime::hierarchyProof(IrSymbolRef child,IrSymbolRef ancestor){return factStore_->hierarchyProof(child,ancestor);}
std::vector<IrSymbolRef> FelidaeKnowledgeRuntime::commonAncestors(IrSymbolRef left,IrSymbolRef right){return factStore_->commonAncestors(left,right);}
std::vector<IrSymbolRef> FelidaeKnowledgeRuntime::leastCommonAncestors(IrSymbolRef left,IrSymbolRef right){return factStore_->leastCommonAncestors(left,right);}
std::vector<IrSymbolRef> FelidaeKnowledgeRuntime::mostGeneralCommonAncestors(IrSymbolRef left,IrSymbolRef right){return factStore_->mostGeneralCommonAncestors(left,right);}
std::vector<VmRankedFact> FelidaeKnowledgeRuntime::rankFacts(IrSymbolRef effectiveAtField,IrSymbolRef priorityField){return factStore_->rankByTimeAndPriority(effectiveAtField,priorityField);}

void FelidaeKnowledgeRuntime::installIsaModule(const IsaModule& module) {
    std::uint64_t hash = 14695981039346656037ull;
    const auto add = [&](std::uint64_t word) { hash ^= word; hash *= 1099511628211ull; };
    add(module.isaVersion); add(module.entryProcedure);
    for (const auto word : module.initializer.code.words) add(word);
    for (const auto& procedure : module.procedures) for (const auto word : procedure.program.code.words) add(word);
    for (const auto symbol : module.procedureSymbols) add(symbol);
    const auto moduleKey = hash == 0 ? 1 : hash;
    if (!modules_.contains(moduleKey)) modules_.emplace(moduleKey, VmModuleState{});
    activeModule_ = moduleKey;
    recordTrace(VmTraceKind::ModuleInstalled, moduleKey);
}

void FelidaeKnowledgeRuntime::enterProcedure(IrSymbolRef procedure,
                                             std::span<const IrSymbolRef> parameters,
                                             std::span<const VmValue> arguments) {
    if (parameters.size() != arguments.size()) throw IrError("ISA procedure received the wrong number of arguments");
    if (procedureDepth_ >= maximumCallDepth_) throw IrError("ISA procedure call depth exceeds its limit");
    ++procedureDepth_;
    recordTrace(VmTraceKind::ProcedureCall, procedure);
    VmCallFrame frame; frame.procedure = procedure;
    for (std::size_t index=0;index<parameters.size();++index) {
        if (!frame.locals.emplace(parameters[index],arguments[index]).second) {
            --procedureDepth_; throw IrError("ISA procedure parameter metadata is duplicated");
        }
    }
    callFrames_.push_back(std::move(frame));
}

void FelidaeKnowledgeRuntime::leaveProcedure() noexcept {
    if (!callFrames_.empty()) callFrames_.pop_back();
    if (procedureDepth_ != 0) --procedureDepth_;
}

void FelidaeKnowledgeRuntime::recordTrace(VmTraceKind kind, IrSymbolRef symbol, IrFactRef fact) {
    if (traces_.size() == maximumTraceEntries_) traces_.erase(traces_.begin());
    traces_.push_back(VmExecutionTrace{nextTraceSequence_++, kind, symbol, fact, procedureDepth_});
}

std::vector<VmExecutionTrace> FelidaeKnowledgeRuntime::executionTraces() const { return traces_; }

VmValue FelidaeKnowledgeRuntime::loadSymbol(IrSymbolRef symbol) {
    // Procedures are lexically isolated: a callee may read its own parameters
    // and locals or module globals, never caller-local bindings.
    if (!callFrames_.empty()) {
        const auto local = callFrames_.back().locals.find(symbol);
        if (local != callFrames_.back().locals.end()) return local->second;
    }
    const auto& globals = modules_.at(activeModule_).globals;
    const auto global = globals.find(symbol);
    if (global == globals.end()) throw IrError("direct VM runtime reads an undefined symbol");
    return global->second;
}

void FelidaeKnowledgeRuntime::storeSymbol(IrSymbolRef symbol, const VmValue& value) {
    if (callFrames_.empty()) {
        auto& globals = modules_.at(activeModule_).globals;
        if (globals.contains(symbol)) {
            throw IrError("direct VM runtime cannot rebind an immutable global");
        }
        globals.emplace(symbol, value);
        return;
    }
    auto& locals = callFrames_.back().locals;
    if (locals.contains(symbol)) {
        throw IrError("direct VM runtime cannot rebind an immutable local");
    }
    locals.emplace(symbol, value);
}

RuntimeStateModel* FelidaeKnowledgeRuntime::runtimeStateModel() { return semanticModel_; }

void FelidaeKnowledgeRuntime::beginExecution() {
    if (executionDepth_++ == 0) {
        recordTrace(VmTraceKind::ExecutionBegin);
        executionState_ = semanticModel_ ? semanticModel_->createExecutionState() : nullptr;
        sharedSemanticSteps_ = std::make_shared<std::size_t>(0);
    }
}

void FelidaeKnowledgeRuntime::endExecution() noexcept {
    if (executionDepth_ == 0) return;
    if (--executionDepth_ == 0) {
        executionState_.reset();
        sharedSemanticSteps_.reset();
    }
}

RuntimeContext FelidaeKnowledgeRuntime::makeIsaRuntimeContext(const VmValue&) const {
    RuntimeContext context;
    context.maximumSemanticSteps = maximumSemanticSteps_;
    context.executionState = executionState_;
    context.sharedSemanticSteps = sharedSemanticSteps_;
    context.knowledge = factStore_->knowledgeSnapshot();
    return context;
}

void FelidaeKnowledgeRuntime::refreshRuntimeContext(RuntimeContext& context) const {
    context.knowledge = factStore_->knowledgeSnapshot();
}
#else
void IrVerifier::verify(const FelidaeIr& ir) {
    if (ir.words.empty()) throw IrError("IR is empty");
    if (ir.words.size() > kMaximumIrWords) throw IrError("IR exceeds its word limit");
    if (ir.registerCount > kMaximumRegisters) throw IrError("IR exceeds its register limit");
    if (ir.constants.size() > kMaximumIrTableEntries || ir.symbols.size() > kMaximumIrTableEntries ||
        ir.programs.size() > kMaximumIrTableEntries || ir.texts.size() > kMaximumIrTableEntries ||
        ir.sourceMap.size() > kMaximumIrTableEntries) {
        throw IrError("IR side table exceeds its entry limit");
    }
    std::size_t textBytes = 0;
    for (const auto& text : ir.texts) {
        if (text.size() > kMaximumTextBytes - textBytes) throw IrError("IR text table exceeds its byte limit");
        textBytes += text.size();
    }
    if (!ir.constantKinds.empty() && ir.constantKinds.size() != ir.constants.size()) {
        throw IrError("IR constant kinds do not match its constant table");
    }

    std::unordered_set<std::size_t> boundaries;
    for (std::size_t scan = 0; scan < ir.words.size();) {
        boundaries.insert(scan);
        const auto opcode = opcodeAt(ir, scan);
        const auto width = (opcode == IrOpcode::Call || opcode == IrOpcode::SemanticEval || opcode == IrOpcode::MakeArray)
            ? dynamicWidth(ir, scan, 1)
            : opcode == IrOpcode::CallNamed || opcode == IrOpcode::MakeMap
                ? dynamicWidth(ir, scan, 2)
            : widthFor(opcode);
        requireWords(ir, scan, width);
        scan += width;
    }
    for (const auto& entry : ir.sourceMap) {
        if (!boundaries.contains(entry.instructionWord)) {
            throw IrError("IR source map does not reference an instruction boundary");
        }
        const auto& span = entry.sourceSpan;
        if (span.startLine <= 0 || span.startColumn <= 0 || span.endLine < span.startLine ||
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
                if (pc + 1 != ir.words.size()) throw IrError("IR has instructions after END");
                ended = true;
                ++pc;
                break;
            case IrOpcode::LoadConst:
                requireWords(ir, pc, 3);
                requireRegister(ir, ir.words[pc + 1]);
                if (ir.words[pc + 2] >= ir.constants.size()) throw IrError("IR references an invalid constant");
                if (!ir.constantKinds.empty() &&
                    ir.constantKinds[ir.words[pc + 2]] == IrConstantKind::Text &&
                    ir.constants[ir.words[pc + 2]] >= ir.texts.size()) {
                    throw IrError("IR references an invalid text constant");
                }
                initialized[ir.words[pc + 1]] = true;
                pc += 3;
                break;
            case IrOpcode::LoadSymbol:
                requireWords(ir, pc, 3);
                requireRegister(ir, ir.words[pc + 1]);
                if (ir.words[pc + 2] >= ir.symbols.size()) throw IrError("IR references an invalid symbol");
                initialized[ir.words[pc + 1]] = true;
                pc += 3;
                break;
            case IrOpcode::StoreSymbol:
                requireWords(ir, pc, 3);
                if (ir.words[pc + 1] >= ir.symbols.size()) throw IrError("IR stores an invalid symbol");
                requireRegister(ir, ir.words[pc + 2]);
                requireInitialized(initialized, ir.words[pc + 2]);
                pc += 3;
                break;
            case IrOpcode::Move:
                requireWords(ir, pc, 3);
                requireRegister(ir, ir.words[pc + 1]);
                requireRegister(ir, ir.words[pc + 2]);
                requireInitialized(initialized, ir.words[pc + 2]);
                initialized[ir.words[pc + 1]] = true;
                pc += 3;
                break;
            case IrOpcode::ForEachFact:
                requireWords(ir, pc, 4);
                requireRegister(ir, ir.words[pc + 1]);
                if (ir.words[pc + 2] >= ir.symbols.size() || ir.words[pc + 3] >= ir.symbols.size()) throw IrError("IR fact loop references an invalid symbol");
                initialized[ir.words[pc + 1]] = true;
                pc += 4;
                break;
            case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div: case IrOpcode::Mod:
                requireWords(ir, pc, 4);
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
                requireWords(ir, pc, 4);
                requireRegister(ir, ir.words[pc + 1]); requireRegister(ir, ir.words[pc + 2]); requireRegister(ir, ir.words[pc + 3]);
                requireInitialized(initialized, ir.words[pc + 2]); requireInitialized(initialized, ir.words[pc + 3]);
                initialized[ir.words[pc + 1]] = true; pc += 4; break;
            case IrOpcode::TemporalRank:
                requireWords(ir, pc, 4);
                requireRegister(ir, ir.words[pc + 1]);
                if (ir.words[pc + 2] >= ir.symbols.size() ||
                    ir.words[pc + 3] >= ir.symbols.size()) {
                    throw IrError("IR temporal rank references an invalid field symbol");
                }
                initialized[ir.words[pc + 1]] = true;
                pc += 4;
                break;
            case IrOpcode::Membership:
                requireWords(ir, pc, 6);
                for (std::size_t i = 1; i < 6; ++i) { requireRegister(ir, ir.words[pc + i]); if (i > 1) requireInitialized(initialized, ir.words[pc + i]); }
                initialized[ir.words[pc + 1]] = true; pc += 6; break;
            case IrOpcode::Compare:
                requireWords(ir, pc, 5);
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
                requireWords(ir, pc, 2);
                if (!boundaries.contains(ir.words[pc + 1])) throw IrError("IR jump target is not an instruction boundary");
                pc += 2;
                break;
            case IrOpcode::JumpIfFalse:
                requireWords(ir, pc, 3);
                requireRegister(ir, ir.words[pc + 1]);
                requireInitialized(initialized, ir.words[pc + 1]);
                if (!boundaries.contains(ir.words[pc + 2])) throw IrError("IR jump target is not an instruction boundary");
                pc += 3;
                break;
            case IrOpcode::CallNative:
                requireWords(ir, pc, 3);
                requireRegister(ir, ir.words[pc + 1]);
                if (ir.words[pc + 2] >= ir.symbols.size()) throw IrError("IR call references an invalid symbol");
                initialized[ir.words[pc + 1]] = true;
                pc += 3;
                break;
            case IrOpcode::Call: {
                requireWords(ir, pc, 4);
                requireRegister(ir, ir.words[pc + 1]);
                if (ir.words[pc + 2] >= ir.symbols.size()) throw IrError("IR call references an invalid symbol");
                const auto count = ir.words[pc + 3];
                const auto width = dynamicWidth(ir, pc, 1);
                for (std::size_t index = 0; index < count; ++index) {
                    requireRegister(ir, ir.words[pc + 4 + index]);
                    requireInitialized(initialized, ir.words[pc + 4 + index]);
                }
                initialized[ir.words[pc + 1]] = true;
                pc += width;
                break;
            }
            case IrOpcode::SemanticEval: {
                requireWords(ir, pc, 4);
                requireRegister(ir, ir.words[pc + 1]);
                if (ir.words[pc + 2] > std::numeric_limits<std::uint16_t>::max() ||
                    !isKnownSemanticOperation(static_cast<std::uint16_t>(ir.words[pc + 2]))) {
                    throw IrError("compiler IR semantic operation ID is invalid");
                }
                const auto count = ir.words[pc + 3];
                if (!semanticOperationAcceptsArity(
                        static_cast<std::uint16_t>(ir.words[pc + 2]), count)) {
                    throw IrError("compiler IR semantic operation arity is invalid");
                }
                const auto width = dynamicWidth(ir, pc, 1);
                for (std::size_t index = 0; index < count; ++index) {
                    requireRegister(ir, ir.words[pc + 4 + index]);
                    requireInitialized(initialized, ir.words[pc + 4 + index]);
                }
                initialized[ir.words[pc + 1]] = true;
                pc += width;
                break;
            }
            case IrOpcode::CallNamed: {
                requireWords(ir, pc, 4);
                requireRegister(ir, ir.words[pc + 1]);
                if (ir.words[pc + 2] >= ir.symbols.size()) throw IrError("IR call references an invalid symbol");
                const auto count = ir.words[pc + 3];
                const auto width = dynamicWidth(ir, pc, 2);
                for (std::size_t index = 0; index < count; ++index) {
                    const auto name = ir.words[pc + 4 + 2 * index];
                    const auto value = ir.words[pc + 5 + 2 * index];
                    if (name != 0 && name - 1 >= ir.symbols.size()) throw IrError("IR call references an invalid argument name");
                    requireRegister(ir, value);
                    requireInitialized(initialized, value);
                }
                initialized[ir.words[pc + 1]] = true;
                pc += width;
                break;
            }
            case IrOpcode::MakeArray: {
                requireWords(ir, pc, 4);
                requireRegister(ir, ir.words[pc + 1]);
                const auto count = ir.words[pc + 3];
                const auto width = dynamicWidth(ir, pc, 1);
                for (std::size_t index = 0; index < count; ++index) {
                    requireRegister(ir, ir.words[pc + 4 + index]);
                    requireInitialized(initialized, ir.words[pc + 4 + index]);
                }
                initialized[ir.words[pc + 1]] = true;
                pc += width;
                break;
            }
            case IrOpcode::MakeMap: {
                requireWords(ir, pc, 4);
                requireRegister(ir, ir.words[pc + 1]);
                const auto count = ir.words[pc + 3];
                const auto width = dynamicWidth(ir, pc, 2);
                for (std::size_t index = 0; index < count; ++index) {
                    const auto symbol = ir.words[pc + 4 + 2 * index];
                    const auto value = ir.words[pc + 5 + 2 * index];
                    if (symbol >= ir.symbols.size()) throw IrError("IR map references an invalid field symbol");
                    requireRegister(ir, value);
                    requireInitialized(initialized, value);
                }
                initialized[ir.words[pc + 1]] = true;
                pc += width;
                break;
            }
            case IrOpcode::MakeFact:
                requireWords(ir, pc, 3);
                requireRegister(ir, ir.words[pc + 1]);
                if (ir.words[pc + 2] >= ir.symbols.size()) throw IrError("IR fact references an invalid symbol");
                initialized[ir.words[pc + 1]] = true;
                pc += 3;
                break;
            case IrOpcode::GetField:
                requireWords(ir, pc, 4);
                requireRegister(ir, ir.words[pc + 1]);
                requireRegister(ir, ir.words[pc + 2]);
                requireInitialized(initialized, ir.words[pc + 2]);
                if (ir.words[pc + 3] >= ir.symbols.size()) throw IrError("IR field references an invalid symbol");
                initialized[ir.words[pc + 1]] = true;
                pc += 4;
                break;
            case IrOpcode::SetField:
                requireWords(ir, pc, 4);
                requireRegister(ir, ir.words[pc + 1]);
                requireRegister(ir, ir.words[pc + 3]);
                requireInitialized(initialized, ir.words[pc + 1]);
                requireInitialized(initialized, ir.words[pc + 3]);
                if (ir.words[pc + 2] >= ir.symbols.size()) throw IrError("IR field references an invalid symbol");
                pc += 4;
                break;
            case IrOpcode::Return:
                requireWords(ir, pc, 3);
                requireRegister(ir, ir.words[pc + 1]);
                requireInitialized(initialized, ir.words[pc + 1]);
                if (ir.words[pc + 2] != 0) throw IrError("IR RETURN reserved operand must be zero");
                pc += 3;
                break;
            default:
                throw IrError("IR opcode is not implemented by this VM build");
        }
        if (ended) break;
    }
    if (!ended) throw IrError("IR is missing END");
    verifyControlFlowInitialization(ir, boundaries);
}
#endif

#ifndef FELIDAE_IR_VERIFIER_ONLY
IrWord encodeIrNumber(double value) noexcept {
    if constexpr (sizeof(IrWord) == sizeof(std::uint64_t)) {
        return static_cast<IrWord>(std::bit_cast<std::uint64_t>(value));
    } else {
        // The v8 container explicitly stores a 32-bit word on the supported
        // Win32 build; retain a compact IEEE-754 payload without aliasing.
        return static_cast<IrWord>(std::bit_cast<std::uint32_t>(static_cast<float>(value)));
    }
}

double decodeIrNumber(IrWord word) noexcept {
    if constexpr (sizeof(IrWord) == sizeof(std::uint64_t)) {
        return std::bit_cast<double>(static_cast<std::uint64_t>(word));
    } else {
        return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(word)));
    }
}

bool VmRuntime::shouldBranchFalse(const VmValue& value) const {
    if (std::holds_alternative<VmNil>(value)) return true;
    if (const auto* control = std::get_if<double>(&value)) {
        if (*control == 0.0) return true;
        if (*control == 1.0) return false;
        throw IrError("VM control value must be exactly 0.0 or 1.0");
    }
    throw IrError("VM branch requires a numeric 0.0 or 1.0 control value");
}

VmValue VmRuntime::loadSymbol(IrSymbolRef) {
    throw IrError("IR symbol loading is unavailable in this runtime");
}

void VmRuntime::storeSymbol(IrSymbolRef, const VmValue&) {
    throw IrError("IR symbol storage is unavailable in this runtime");
}

VmValue VmRuntime::callNativeSymbol(IrSymbolRef) {
    throw IrError("ISA native call is unavailable in this runtime");
}

void VmRuntime::retainFact(const VmFactPtr&) {}

void VmRuntime::mutateFact(const VmFactPtr& fact, IrSymbolRef field, const VmValue& value) {
    if (!fact) throw IrError("IR mutation requires a fact value");
    for (auto& [existing, previous] : fact->fields) {
        if (existing == field) { previous = value; return; }
    }
    fact->fields.emplace_back(field, value);
}

void VmRuntime::registerFactType(IrSymbolRef, std::vector<IrSymbolRef>) {}

std::vector<VmFactPtr> VmRuntime::snapshotFacts(IrSymbolRef) {
    throw IrError("IR fact iteration is unavailable in this runtime");
}

std::vector<IrSymbolRef> VmRuntime::hierarchyProof(IrSymbolRef,IrSymbolRef){throw IrError("ISA hierarchy is unavailable in this runtime");}
std::vector<IrSymbolRef> VmRuntime::commonAncestors(IrSymbolRef,IrSymbolRef){throw IrError("ISA hierarchy is unavailable in this runtime");}
std::vector<IrSymbolRef> VmRuntime::leastCommonAncestors(IrSymbolRef,IrSymbolRef){throw IrError("ISA hierarchy is unavailable in this runtime");}
std::vector<IrSymbolRef> VmRuntime::mostGeneralCommonAncestors(IrSymbolRef,IrSymbolRef){throw IrError("ISA hierarchy is unavailable in this runtime");}
std::vector<VmRankedFact> VmRuntime::rankFacts(IrSymbolRef,IrSymbolRef){throw IrError("ISA temporal ranking is unavailable in this runtime");}

void VmRuntime::installIsaModule(const IsaModule&) {}

void VmRuntime::enterProcedure(IrSymbolRef, std::span<const IrSymbolRef> parameters,
                               std::span<const VmValue> arguments) {
    if (parameters.size()!=arguments.size()) throw IrError("ISA procedure received the wrong number of arguments");
}

void VmRuntime::leaveProcedure() noexcept {}

void VmRuntime::recordTrace(VmTraceKind, IrSymbolRef, IrFactRef) {}

RuntimeStateModel* VmRuntime::runtimeStateModel() { return nullptr; }

void VmRuntime::beginExecution() {}

void VmRuntime::endExecution() noexcept {}

RuntimeContext VmRuntime::makeIsaRuntimeContext(const VmValue&) const { return {}; }

void VmRuntime::refreshRuntimeContext(RuntimeContext&) const {}

bool VmRuntime::validateSemanticResult(const VmValue& value, const RuntimeContext&) const {
    return validVmValue(value);
}

VmValue RegisterVm::executeIsaMain(const IsaModule& module, VmRuntime& runtime,
                                   VmValue systemInput) {
    verifyIsaModule(module);
#ifndef NDEBUG
    if (vmTraceEnabled()) {
        std::clog << "[felidae.vm] action=execute_verified_module isa_version="
                  << module.isaVersion << " procedures=" << module.procedures.size()
                  << " entry_index=" << module.entryProcedure << '\n';
    }
#endif
    runtime.installIsaModule(module);
    for (const auto& type : module.factTypes) runtime.registerFactType(type.symbol, type.parents);
    runtime.beginExecution();
    struct Scope { VmRuntime& runtime; ~Scope(){runtime.endExecution();} } scope{runtime};
    return executeIsaProgram(module,module.initializer,runtime,std::move(systemInput),0);
}

VmValue RegisterVm::executeIsaProgram(const IsaModule& module, const IsaProgram& program,
                                      VmRuntime& runtime, VmValue systemInput,
                                      std::size_t callDepth) {
    if(callDepth>256)throw IrError("ISA procedure call depth exceeds VM limit");
    IsaVerifier::verify(program.code,{program.constants.size(),program.symbols.size(),module.procedures.size()});
    std::vector<VmValue> registers(program.code.registerCount,VmNil{});
    auto semanticContext = runtime.makeIsaRuntimeContext(systemInput);
    const auto symbol=[&](std::size_t index){return program.symbols.at(index);};
    const auto argumentRegister=[&](std::size_t pc,std::size_t index){return static_cast<std::uint8_t>((program.code.words.at(pc+2+index/4)>>((index%4)*8u))&0xffu);};
    const auto typeSymbol=[&](const VmValue& value)->IrSymbolRef{if(const auto item=std::get_if<VmSymbol>(&value)){if(item->value==0)throw IrError("ISA hierarchy received an invalid symbol");return item->value;}if(const auto fact=std::get_if<VmFactPtr>(&value);fact&&*fact)return(*fact)->type;throw IrError("ISA hierarchy operands must be symbols or facts");};
    const auto symbolArray=[](const std::vector<IrSymbolRef>& symbols){auto result=std::make_shared<VmArray>();result->values.reserve(symbols.size());for(const auto value:symbols)result->values.push_back(VmSymbol{value});return result;};
    const auto invoke=[&](std::uint16_t procedureIndex,std::vector<VmValue> arguments)->VmValue{
        const auto& procedure=module.procedures.at(procedureIndex);
        runtime.enterProcedure(module.procedureSymbols.at(procedureIndex),procedure.positionalParameters,arguments);
        struct FrameScope{VmRuntime& runtime;~FrameScope(){runtime.leaveProcedure();}} frame{runtime};
        return executeIsaProgram(module,procedure.program,runtime,VmNil{},callDepth+1);
    };
    for(std::size_t pc=0;pc<program.code.words.size();){
        const auto d=decodeIsaWord(program.code.words[pc]);
        const auto instructionWidth=isaInstructionWidth(program.code.words,pc);
#ifndef NDEBUG
        if(vmTraceEnabled()){
            std::clog<<"[felidae.vm] action=dispatch depth="<<callDepth<<" pc="<<pc
                     <<" opcode=0x"<<std::hex<<std::setw(2)<<std::setfill('0')
                     <<static_cast<unsigned>(d.opcode)<<std::dec<<std::setfill(' ')
                     <<" width="<<instructionWidth<<'\n';
        }
#endif
        switch(d.opcode){
        case IsaOpcode::Halt:
            // Compiler END lowers to HALT as an unreachable structural
            // terminator after Return. Reaching it means lowering produced no
            // program result; never report that defect as a successful nil.
            throw IrError("ISA halted without returning a value");
        case IsaOpcode::LoadConstant:{
            const auto index=d.bx;const auto kind=program.constantKinds.empty()?IrConstantKind::Number:program.constantKinds.at(index);
            if(kind==IrConstantKind::Number)registers[d.a]=decodeIrNumber(program.constants.at(index));
            else if(kind==IrConstantKind::Boolean)registers[d.a]=program.constants.at(index)!=0?1.0:0.0;
            else if(kind==IrConstantKind::Nil)registers[d.a]=VmNil{};
            else registers[d.a]=VmText{program.texts.at(program.constants.at(index))};
            break;}
        case IsaOpcode::LoadGlobal:registers[d.a]=runtime.loadSymbol(symbol(d.bx));break;
        case IsaOpcode::StoreGlobal:runtime.storeSymbol(symbol(d.bx),registers[d.a]);break;
        case IsaOpcode::Move:registers[d.a]=registers[d.b];break;
        case IsaOpcode::Add:case IsaOpcode::Subtract:case IsaOpcode::Multiply:case IsaOpcode::Divide:case IsaOpcode::Modulo:{
            const auto lhs=std::get_if<double>(&registers[d.b]);const auto rhs=std::get_if<double>(&registers[d.c]);if(!lhs||!rhs)throw IrError("ISA arithmetic operands must be numbers");double value=0;
            if(d.opcode==IsaOpcode::Add)value=*lhs+*rhs;else if(d.opcode==IsaOpcode::Subtract)value=*lhs-*rhs;else if(d.opcode==IsaOpcode::Multiply)value=*lhs**rhs;else if(d.opcode==IsaOpcode::Divide){if(*rhs==0)throw IrError("ISA division by zero");value=*lhs / *rhs;}else{if(*rhs==0)throw IrError("ISA modulo by zero");value=std::fmod(*lhs,*rhs);}registers[d.a]=value;break;}
        case IsaOpcode::CompareEqual:case IsaOpcode::CompareNotEqual:case IsaOpcode::CompareLess:case IsaOpcode::CompareLessEqual:case IsaOpcode::CompareGreater:case IsaOpcode::CompareGreaterEqual:{
            bool value=false;if(d.opcode==IsaOpcode::CompareEqual||d.opcode==IsaOpcode::CompareNotEqual){value=vmValuesEqual(registers[d.b],registers[d.c]);if(d.opcode==IsaOpcode::CompareNotEqual)value=!value;}else{const auto numeric=[](const VmValue& v)->std::optional<double>{if(const auto n=std::get_if<double>(&v))return *n;if(const auto n=std::get_if<VmDegree>(&v))return n->value;return std::nullopt;};const auto l=numeric(registers[d.b]),r=numeric(registers[d.c]);if(!l||!r)throw IrError("ISA ordered comparison requires numeric operands");if(d.opcode==IsaOpcode::CompareLess)value=*l<*r;else if(d.opcode==IsaOpcode::CompareLessEqual)value=*l<=*r;else if(d.opcode==IsaOpcode::CompareGreater)value=*l>*r;else value=*l>=*r;}registers[d.a]=value?1.0:0.0;break;}
        case IsaOpcode::BooleanNot:{const auto value=std::get_if<double>(&registers[d.b]);if(!value||(*value!=0.0&&*value!=1.0))throw IrError("ISA boolean NOT requires 0.0 or 1.0");registers[d.a]=*value==0.0?1.0:0.0;break;}
        case IsaOpcode::BooleanAnd:case IsaOpcode::BooleanOr:{const auto l=std::get_if<double>(&registers[d.b]),r=std::get_if<double>(&registers[d.c]);if(!l||!r||(*l!=0.0&&*l!=1.0)||(*r!=0.0&&*r!=1.0))throw IrError("ISA boolean logic requires 0.0 or 1.0");registers[d.a]=(d.opcode==IsaOpcode::BooleanAnd?(*l==1.0&&*r==1.0):(*l==1.0||*r==1.0))?1.0:0.0;break;}
        case IsaOpcode::Jump:pc=d.ax;continue;
        case IsaOpcode::JumpIfFalse:if(runtime.shouldBranchFalse(registers[d.a])){pc=d.bx;continue;}break;
        case IsaOpcode::Call:{std::vector<VmValue> args;args.reserve(d.b);for(std::size_t i=0;i<d.b;++i)args.push_back(registers[argumentRegister(pc,i)]);registers[d.a]=invoke(static_cast<std::uint16_t>(program.code.words[pc+1]),std::move(args));break;}
        case IsaOpcode::CallNamed:{
            const auto procedureIndex=static_cast<std::uint16_t>(program.code.words[pc+1]);const auto& procedure=module.procedures.at(procedureIndex);std::vector<std::optional<VmValue>> mapped(procedure.positionalParameters.size());std::size_t next=0;
            for(std::size_t i=0;i<d.b;++i){const auto encoded=program.code.words[pc+2+i];const auto name=static_cast<std::uint16_t>(encoded);std::size_t targetIndex=0;if(name==0xffffu){while(next<mapped.size()&&mapped[next])++next;if(next==mapped.size())throw IrError("ISA named call has too many positional arguments");targetIndex=next++;}else{const auto spelling=symbol(name);const auto found=std::find(procedure.namedParameters.begin(),procedure.namedParameters.end(),spelling);if(found==procedure.namedParameters.end())throw IrError("ISA named call has an unknown argument");targetIndex=static_cast<std::size_t>(found-procedure.namedParameters.begin());}if(mapped[targetIndex])throw IrError("ISA named call duplicates an argument");mapped[targetIndex]=registers[static_cast<std::uint8_t>(encoded>>16u)];}
            std::vector<VmValue> args;for(auto& value:mapped){if(!value)throw IrError("ISA named call omits an argument");args.push_back(std::move(*value));}registers[d.a]=invoke(procedureIndex,std::move(args));break;}
        case IsaOpcode::CallNative:registers[d.a]=runtime.callNativeSymbol(symbol(d.bx));break;
        case IsaOpcode::Return:return registers[d.a];
        case IsaOpcode::MakeFact:{auto fact=std::make_shared<VmFact>();fact->type=symbol(d.bx);runtime.retainFact(fact);registers[d.a]=std::move(fact);break;}
        case IsaOpcode::GetField:{const auto field=symbol(program.code.words[pc+1]);const auto read=[&](const auto& entries)->VmValue{for(const auto& [key,value]:entries)if(key==field)return value;throw IrError("ISA field is absent");};if(const auto fact=std::get_if<VmFactPtr>(&registers[d.b]);fact&&*fact)registers[d.a]=read((*fact)->fields);else if(const auto map=std::get_if<VmMapPtr>(&registers[d.b]);map&&*map)registers[d.a]=read((*map)->entries);else throw IrError("ISA field access requires a fact or map");break;}
        case IsaOpcode::SetField:{const auto field=symbol(program.code.words[pc+1]);const auto value=registers[d.b];if(const auto fact=std::get_if<VmFactPtr>(&registers[d.a]);fact&&*fact)runtime.mutateFact(*fact,field,value);else if(const auto map=std::get_if<VmMapPtr>(&registers[d.a]);map&&*map){auto found=std::find_if((*map)->entries.begin(),(*map)->entries.end(),[&](const auto& e){return e.first==field;});if(found==(*map)->entries.end())(*map)->entries.emplace_back(field,value);else found->second=value;}else throw IrError("ISA field assignment requires a fact or map");break;}
        case IsaOpcode::QueryFacts:{const auto extension=program.code.words[pc+1];const auto facts=runtime.snapshotFacts(symbol(extension&0xffffu));auto values=std::make_shared<VmArray>();for(const auto& fact:facts)values->values.push_back(invoke(static_cast<std::uint16_t>(extension>>16u),{VmValue{fact}}));registers[d.a]=std::move(values);break;}
        case IsaOpcode::MakeArray:{auto array=std::make_shared<VmArray>();for(std::size_t i=0;i<d.b;++i)array->values.push_back(registers[static_cast<std::uint8_t>((program.code.words[pc+1+i/4]>>((i%4)*8u))&0xffu)]);registers[d.a]=std::move(array);break;}
        case IsaOpcode::MakeMap:{auto map=std::make_shared<VmMap>();for(std::size_t i=0;i<d.b;++i){const auto entry=program.code.words[pc+1+i];map->entries.emplace_back(symbol(entry&0xffffu),registers[static_cast<std::uint8_t>(entry>>16u)]);}registers[d.a]=std::move(map);break;}
        case IsaOpcode::Similarity:registers[d.a]=VmDegree(similarityDegree(registers[d.b],registers[d.c]));break;
        case IsaOpcode::Membership:{const auto tail=program.code.words[pc+1];const auto number=[&](std::uint8_t r){const auto n=std::get_if<double>(&registers[r]);if(!n)throw IrError("ISA membership operands must be numbers");return *n;};registers[d.a]=VmDegree(VmFactStore::gaussianMembership(number(d.b),{number(d.c),number(static_cast<std::uint8_t>(tail)),number(static_cast<std::uint8_t>(tail>>8u))}));break;}
        case IsaOpcode::SemanticEval:{
            const auto operation=static_cast<std::uint16_t>(program.code.words[pc+1]);
            auto* model=runtime.runtimeStateModel();
            if(!model)throw IrError("ISA SemanticEval requires a RuntimeStateModel");
            runtime.refreshRuntimeContext(semanticContext);
            const auto steps=semanticContext.sharedSemanticSteps?*semanticContext.sharedSemanticSteps:semanticContext.semanticSteps;
            if(steps>=semanticContext.maximumSemanticSteps)throw IrError("ISA semantic operation exceeds execution state limit");
            std::vector<VmValue> inputs;
            for(std::size_t i=0;i<d.b;++i)inputs.push_back(registers[argumentRegister(pc,i)]);
            if(!validSemanticInputs(operation,inputs))throw IrError("ISA SemanticEval input contract is invalid");
            if(semanticContext.sharedSemanticSteps)semanticContext.semanticSteps=++*semanticContext.sharedSemanticSteps;
            else ++semanticContext.semanticSteps;
            auto value=model->evaluate(RuntimeOperation{operation},inputs,semanticContext);
            if(!validSemanticOutput(operation,inputs,value)||!runtime.validateSemanticResult(value,semanticContext))throw IrError("RuntimeStateModel returned an invalid typed result");
            if(const auto fact=std::get_if<VmFactPtr>(&value);fact&&*fact){(*fact)->origin=VmFact::Origin::Derived;runtime.retainFact(*fact);}
            runtime.recordTrace(VmTraceKind::SsmProposal,operation);
            registers[d.a]=std::move(value);
            break;}
        case IsaOpcode::HierarchyIsA:registers[d.a]=runtime.hierarchyProof(typeSymbol(registers[d.b]),typeSymbol(registers[d.c])).empty()?0.0:1.0;break;
        case IsaOpcode::HierarchyCommonAncestors:registers[d.a]=symbolArray(runtime.commonAncestors(typeSymbol(registers[d.b]),typeSymbol(registers[d.c])));break;
        case IsaOpcode::HierarchyLeastCommonAncestors:registers[d.a]=symbolArray(runtime.leastCommonAncestors(typeSymbol(registers[d.b]),typeSymbol(registers[d.c])));break;
        case IsaOpcode::HierarchyMostGeneralAncestors:registers[d.a]=symbolArray(runtime.mostGeneralCommonAncestors(typeSymbol(registers[d.b]),typeSymbol(registers[d.c])));break;
        case IsaOpcode::TemporalRank:{const auto fields=program.code.words[pc+1];const auto ranked=runtime.rankFacts(symbol(fields&0xffffu),symbol(fields>>16u));auto values=std::make_shared<VmArray>();values->values.reserve(ranked.size());for(const auto& item:ranked)values->values.push_back(item.fact);registers[d.a]=std::move(values);break;}
        }
        pc+=instructionWidth;
    }
    throw IrError("ISA program ended without Return or Halt");
}
#endif

} // namespace Felidae
