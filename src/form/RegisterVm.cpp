#include "RegisterVm.h"

#include "IrModule.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <sstream>
#include <limits>
#include <unordered_set>

namespace Felidae {

RuntimeValueKind runtimeValueKind(const VmValue& value) noexcept {
    if (std::holds_alternative<VmNil>(value)) return RuntimeValueKind::Nil;
    if (std::holds_alternative<bool>(value)) return RuntimeValueKind::Boolean;
    if (std::holds_alternative<double>(value)) return RuntimeValueKind::Number;
    if (std::holds_alternative<VmDegree>(value)) return RuntimeValueKind::Degree;
    if (std::holds_alternative<VmText>(value)) return RuntimeValueKind::Text;
    if (std::holds_alternative<VmArrayPtr>(value)) return RuntimeValueKind::Array;
    if (std::holds_alternative<VmMapPtr>(value)) return RuntimeValueKind::Map;
    return RuntimeValueKind::Fact;
}
VmDegree::VmDegree(double value) : value(value) {
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) throw IrError("Degree must be finite and within [0,1]");
}
namespace {

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
    case IrOpcode::Call: case IrOpcode::CallNamed: case IrOpcode::SemanticEval: case IrOpcode::SsmProcess:
        return 0; // validated below from explicit argument count
    case IrOpcode::MakeArray: return 0; // destination, explicit item count, registers
    case IrOpcode::MakeMap: return 0; // destination, explicit entry count, symbol/register pairs
    case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div: case IrOpcode::Mod:
    case IrOpcode::GetField: case IrOpcode::SetField: case IrOpcode::Similarity: return 4;
    case IrOpcode::Membership: return 6;
    case IrOpcode::Compare: return 5;
    }
    throw IrError("IR contains an invalid opcode");
}

void requireFlowRead(const std::vector<bool>& initialized, IrWord registerId) {
    if (!initialized[registerId]) throw IrError("IR control-flow path reads an uninitialized register");
}

bool vmValuesEqual(const VmValue& left, const VmValue& right) {
    if (left.index() != right.index()) return false;
    if (std::holds_alternative<VmNil>(left)) return true;
    if (const auto* value = std::get_if<bool>(&left)) return *value == std::get<bool>(right);
    if (const auto* value = std::get_if<double>(&left)) return *value == std::get<double>(right);
    if (const auto* value = std::get_if<VmDegree>(&left)) return *value == std::get<VmDegree>(right);
    if (const auto* value = std::get_if<VmText>(&left)) return *value == std::get<VmText>(right);
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
    const auto clamp = [](double value) { return std::clamp(value, 0.0, 1.0); };
    if (const auto a = std::get_if<double>(&left)) {
        if (const auto b = std::get_if<double>(&right)) return 1.0 / (1.0 + std::abs(*a - *b));
    }
    if (const auto a = std::get_if<VmDegree>(&left)) {
        if (const auto b = std::get_if<VmDegree>(&right)) return 1.0 - std::abs(a->value - b->value);
    }
    if (const auto a = std::get_if<VmText>(&left)) {
        if (const auto b = std::get_if<VmText>(&right)) {
            const auto limit = std::max(a->pieces.size(), b->pieces.size());
            if (limit == 0) return 1.0;
            std::size_t same = 0;
            for (const auto piece : a->pieces) if (std::find(b->pieces.begin(), b->pieces.end(), piece) != b->pieces.end()) ++same;
            return static_cast<double>(same) / static_cast<double>(limit);
        }
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
        const auto next = pc + (op == IrOpcode::Call || op == IrOpcode::SemanticEval || op == IrOpcode::SsmProcess || op == IrOpcode::MakeArray
            ? dynamicWidth(ir, pc, 1)
            : op == IrOpcode::CallNamed || op == IrOpcode::MakeMap ? dynamicWidth(ir, pc, 2) : widthFor(op));
        auto write = [&](IrWord target) { state[target] = true; };
        switch (op) {
        case IrOpcode::LoadConst: case IrOpcode::LoadSymbol: case IrOpcode::ForEachFact: write(ir.words[pc + 1]); break;
        case IrOpcode::StoreSymbol: requireFlowRead(state, ir.words[pc + 2]); break;
        case IrOpcode::Move: requireFlowRead(state, ir.words[pc + 2]); write(ir.words[pc + 1]); break;
        case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div: case IrOpcode::Mod:
        case IrOpcode::Similarity: case IrOpcode::Membership:
        case IrOpcode::Compare:
            requireFlowRead(state, ir.words[pc + 2]); requireFlowRead(state, ir.words[pc + 3]); write(ir.words[pc + 1]); break;
        case IrOpcode::JumpIfFalse: requireFlowRead(state, ir.words[pc + 1]); break;
        case IrOpcode::Call:
            for (std::size_t i = 0; i < ir.words[pc + 3]; ++i) requireFlowRead(state, ir.words[pc + 4 + i]);
            write(ir.words[pc + 1]); break;
        case IrOpcode::SemanticEval: case IrOpcode::SsmProcess:
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

} // namespace

std::string vmValueToDisplayString(const VmValue& value, const VmDisplayContext& context) {
    const auto render = [&](const auto& self, const VmValue& item) -> std::string {
        if (std::holds_alternative<VmNil>(item)) return "nil";
        if (const auto boolean = std::get_if<bool>(&item)) return *boolean ? "true" : "false";
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
        if (const auto text = std::get_if<VmText>(&item)) {
            if (!context.textDecoder) throw IrError("VM text display requires an injected SentencePiece decoder");
            return context.textDecoder(text->pieces);
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
    for (const auto& fact : facts_) ranked.push_back({fact, numberField(fact, effectiveAtField, "fx.observed_at"), numberField(fact, priorityField, "priority")});
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

namespace {
std::uint64_t moduleFingerprint(const IrModule& module) {
    std::uint64_t hash = 14695981039346656037ull;
    const auto add = [&](std::uint64_t word) { hash ^= word; hash *= 1099511628211ull; };
    add(module.entryProcedure);
    for (const auto word : module.ir.words) add(word);
    for (std::size_t index = 0; index < module.ir.constants.size(); ++index) {
        add(module.ir.constants[index]);
        add(module.ir.constantKinds.empty() ? 0 : static_cast<std::uint64_t>(module.ir.constantKinds[index]) + 1);
    }
    for (const auto& text : module.ir.texts) for (const auto piece : text) add(piece);
    for (const auto symbol : module.ir.symbols) add(symbol);
    std::vector<IrSymbolRef> symbols;
    symbols.reserve(module.procedures.size());
    for (const auto& [symbol, _] : module.procedures) symbols.push_back(symbol);
    std::sort(symbols.begin(), symbols.end());
    for (const auto symbol : symbols) {
        add(symbol);
        const auto& procedure = module.procedures.at(symbol);
        for (const auto word : procedure.ir.words) add(word);
        for (const auto word : procedure.ir.constants) add(word);
        for (const auto symbolRef : procedure.ir.symbols) add(symbolRef);
    }
    for (const auto& type : module.factTypes) {
        add(type.symbol);
        for (const auto parent : type.parents) add(parent);
    }
    return hash == 0 ? 1 : hash;
}
}

FelidaeKnowledgeRuntime::FelidaeKnowledgeRuntime(std::unordered_map<IrSymbolRef, IrProcedure> procedures,
                                 RuntimeStateModel* semanticModel,
                                 std::size_t maximumSemanticSteps,
                                 std::size_t maximumCallDepth,
                                 std::shared_ptr<VmFactStore> factStore)
    : semanticModel_(semanticModel),
      factStore_(factStore ? std::move(factStore) : std::make_shared<VmFactStore>()),
      maximumSemanticSteps_(maximumSemanticSteps), maximumCallDepth_(maximumCallDepth) {
    if (maximumSemanticSteps_ == 0) throw IrError("direct VM semantic step limit must be positive");
    if (maximumCallDepth_ == 0) throw IrError("direct VM call depth limit must be positive");
    for (const auto& [_, procedure] : procedures) IrVerifier::verify(procedure.ir);
    if (!procedures.empty()) modules_.emplace(0, VmModuleState{{}, std::move(procedures)});
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

void FelidaeKnowledgeRuntime::installModule(const IrModule& module) {
    // RegisterVm::executeMain verifies the complete module before calling this
    // hook. Keeping registration independent of BinaryIr.cpp lets the Form
    // VM remain linkable for direct verified-IR tests and embedded hosts.
    const auto moduleKey = moduleFingerprint(module);
    if (!modules_.contains(moduleKey)) modules_.emplace(moduleKey, VmModuleState{{}, module.procedures});
    activeModule_ = moduleKey;
    recordTrace(VmTraceKind::ModuleInstalled, moduleKey);
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

RuntimeContext FelidaeKnowledgeRuntime::makeRuntimeContext(const FelidaeIr&, const VmValue&) const {
    RuntimeContext context;
    context.maximumSemanticSteps = maximumSemanticSteps_;
    context.executionState = executionState_;
    context.sharedSemanticSteps = sharedSemanticSteps_;
    return context;
}

VmValue FelidaeKnowledgeRuntime::callSymbol(IrSymbolRef symbol, std::span<const VmValue> arguments) {
    const auto& procedures = modules_.at(activeModule_).procedures;
    const auto procedure = procedures.find(symbol);
    if (procedure == procedures.end()) throw IrError("direct VM runtime calls an unregistered procedure");
    if (arguments.size() != procedure->second.positionalParameters.size()) {
        throw IrError("direct VM procedure received the wrong number of arguments");
    }
    if (procedureDepth_ >= maximumCallDepth_) throw IrError("direct VM procedure call depth exceeds its limit");
    ++procedureDepth_;
    recordTrace(VmTraceKind::ProcedureCall, symbol);
    VmCallFrame frame;
    frame.procedure = symbol;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        frame.locals.emplace(procedure->second.positionalParameters[index], arguments[index]);
    }
    callFrames_.push_back(std::move(frame));
    try {
        RegisterVm nestedVm;
        auto result = nestedVm.execute(procedure->second.ir, *this, VmNil{});
        callFrames_.pop_back();
        --procedureDepth_;
        return result;
    } catch (...) {
        callFrames_.pop_back();
        --procedureDepth_;
        throw;
    }
}

VmValue FelidaeKnowledgeRuntime::callSymbolNamed(IrSymbolRef symbol,
                                         std::span<const VmCallArgument> arguments) {
    const auto& procedures = modules_.at(activeModule_).procedures;
    const auto procedure = procedures.find(symbol);
    if (procedure == procedures.end()) throw IrError("direct VM runtime calls an unregistered procedure");
    const auto& positionalParameters = procedure->second.positionalParameters;
    const auto& namedParameters = procedure->second.namedParameters;
    if (positionalParameters.size() != namedParameters.size()) {
        throw IrError("direct VM procedure has inconsistent parameter metadata");
    }
    std::vector<std::optional<VmValue>> mapped(positionalParameters.size());
    std::size_t nextPositional = 0;
    for (const auto& argument : arguments) {
        std::size_t index = 0;
        if (argument.name) {
            const auto found = std::find(namedParameters.begin(), namedParameters.end(), *argument.name);
            if (found == namedParameters.end()) throw IrError("direct VM call has an unknown named argument");
            index = static_cast<std::size_t>(found - namedParameters.begin());
        } else {
            while (nextPositional < mapped.size() && mapped[nextPositional]) ++nextPositional;
            if (nextPositional >= mapped.size()) throw IrError("direct VM call has too many positional arguments");
            index = nextPositional++;
        }
        if (mapped[index]) throw IrError("direct VM call assigns a parameter more than once");
        mapped[index] = argument.value;
    }
    std::vector<VmValue> ordered;
    ordered.reserve(mapped.size());
    for (const auto& value : mapped) {
        if (!value) throw IrError("direct VM call is missing a required parameter");
        ordered.push_back(*value);
    }
    return callSymbol(symbol, ordered);
}

void IrVerifier::verify(const FelidaeIr& ir) {
    if (ir.words.empty()) throw IrError("IR is empty");
    if (ir.words.size() > kMaximumIrWords) throw IrError("IR exceeds its word limit");
    if (ir.registerCount > kMaximumRegisters) throw IrError("IR exceeds its register limit");
    if (ir.constants.size() > kMaximumIrTableEntries || ir.symbols.size() > kMaximumIrTableEntries ||
        ir.programs.size() > kMaximumIrTableEntries || ir.texts.size() > kMaximumIrTableEntries ||
        ir.sourceMap.size() > kMaximumIrTableEntries) {
        throw IrError("IR side table exceeds its entry limit");
    }
    std::size_t textWords = 0;
    for (const auto& text : ir.texts) {
        if (text.size() > kMaximumTextBytes / sizeof(std::uint32_t) - textWords) throw IrError("IR text table exceeds its word limit");
        textWords += text.size();
    }
    if (!ir.constantKinds.empty() && ir.constantKinds.size() != ir.constants.size()) {
        throw IrError("IR constant kinds do not match its constant table");
    }

    std::unordered_set<std::size_t> boundaries;
    for (std::size_t scan = 0; scan < ir.words.size();) {
        boundaries.insert(scan);
        const auto opcode = opcodeAt(ir, scan);
        const auto width = (opcode == IrOpcode::Call || opcode == IrOpcode::SemanticEval || opcode == IrOpcode::SsmProcess || opcode == IrOpcode::MakeArray)
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
                requireWords(ir, pc, 4);
                requireRegister(ir, ir.words[pc + 1]); requireRegister(ir, ir.words[pc + 2]); requireRegister(ir, ir.words[pc + 3]);
                requireInitialized(initialized, ir.words[pc + 2]); requireInitialized(initialized, ir.words[pc + 3]);
                initialized[ir.words[pc + 1]] = true; pc += 4; break;
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
            case IrOpcode::SemanticEval: case IrOpcode::SsmProcess: {
                requireWords(ir, pc, 4);
                requireRegister(ir, ir.words[pc + 1]);
                if (ir.words[pc + 2] >= ir.symbols.size()) throw IrError("IR semantic operation references an invalid symbol");
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

IrWord encodeIrNumber(double value) noexcept { return std::bit_cast<IrWord>(value); }
double decodeIrNumber(IrWord word) noexcept { return std::bit_cast<double>(word); }

bool VmRuntime::shouldBranchFalse(const VmValue& value) const {
    if (std::holds_alternative<VmNil>(value)) return true;
    if (const auto* boolean = std::get_if<bool>(&value)) return !*boolean;
    return false;
}

VmValue VmRuntime::loadSymbol(IrSymbolRef) {
    throw IrError("IR symbol loading is unavailable in this runtime");
}

void VmRuntime::storeSymbol(IrSymbolRef, const VmValue&) {
    throw IrError("IR symbol storage is unavailable in this runtime");
}

VmValue VmRuntime::callSymbol(IrSymbolRef, std::span<const VmValue>) {
    throw IrError("IR calls are unavailable in this runtime");
}

VmValue VmRuntime::callSymbolNamed(IrSymbolRef, std::span<const VmCallArgument>) {
    throw IrError("IR named calls are unavailable in this runtime");
}

VmValue VmRuntime::callNativeSymbol(IrSymbolRef symbol) {
    return callSymbol(symbol, {});
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

void VmRuntime::installModule(const IrModule&) {}

void VmRuntime::recordTrace(VmTraceKind, IrSymbolRef, IrFactRef) {}

RuntimeStateModel* VmRuntime::runtimeStateModel() { return nullptr; }

void VmRuntime::beginExecution() {}

void VmRuntime::endExecution() noexcept {}

RuntimeContext VmRuntime::makeRuntimeContext(const FelidaeIr&, const VmValue&) const {
    return {};
}

bool VmRuntime::validateSemanticResult(const VmValue& value, const RuntimeContext&) const {
    return validVmValue(value);
}

VmValue RegisterVm::execute(const FelidaeIr& ir, VmRuntime& runtime, VmValue systemInput) {
    IrVerifier::verify(ir);
    runtime.beginExecution();
    struct ExecutionScope {
        VmRuntime& runtime;
        ~ExecutionScope() { runtime.endExecution(); }
    } executionScope{runtime};
    // Registers are execution-local. RegisterVm is therefore reusable across
    // requests without carrying one invocation's register contents into the
    // next; concurrency still requires distinct runtime instances.
    std::vector<VmValue> registers_(ir.registerCount, VmNil{});
    auto semanticContext = runtime.makeRuntimeContext(ir, systemInput);
    for (std::size_t pc = 0; pc < ir.words.size();) {
        const auto op = static_cast<IrOpcode>(ir.words[pc]);
        switch (op) {
            case IrOpcode::End:
                throw IrError("IR program completed without a result");
            case IrOpcode::LoadConst:
                {
                    const auto constant = ir.words.at(pc + 2);
                    const auto kind = ir.constantKinds.empty() ? IrConstantKind::Number
                        : ir.constantKinds.at(constant);
                    if (kind == IrConstantKind::Number) {
                        registers_.at(ir.words.at(pc + 1)) = decodeIrNumber(ir.constants.at(constant));
                    } else if (kind == IrConstantKind::Boolean) {
                        registers_.at(ir.words.at(pc + 1)) = ir.constants.at(constant) != 0;
                    } else if (kind == IrConstantKind::Nil) {
                        registers_.at(ir.words.at(pc + 1)) = VmNil{};
                    } else {
                        if (ir.constants.at(constant) >= ir.texts.size()) {
                            throw IrError("IR text constant reference is invalid");
                        }
                        registers_.at(ir.words.at(pc + 1)) = VmText{ir.texts.at(ir.constants.at(constant))};
                    }
                }
                pc += 3;
                break;
            case IrOpcode::LoadSymbol:
                registers_.at(ir.words.at(pc + 1)) = runtime.loadSymbol(
                    ir.symbols.at(ir.words.at(pc + 2)));
                pc += 3;
                break;
            case IrOpcode::StoreSymbol:
                runtime.storeSymbol(ir.symbols.at(ir.words.at(pc + 1)),
                                    registers_.at(ir.words.at(pc + 2)));
                pc += 3;
                break;
            case IrOpcode::CallNative:
                registers_.at(ir.words.at(pc + 1)) = runtime.callNativeSymbol(
                    ir.symbols.at(ir.words.at(pc + 2)));
                pc += 3;
                break;
            case IrOpcode::Call: {
                const auto destination = ir.words.at(pc + 1);
                const auto symbol = ir.symbols.at(ir.words.at(pc + 2));
                const auto count = ir.words.at(pc + 3);
                std::vector<VmValue> arguments;
                arguments.reserve(count);
                for (std::size_t index = 0; index < count; ++index) {
                    arguments.push_back(registers_.at(ir.words.at(pc + 4 + index)));
                }
                registers_.at(destination) = runtime.callSymbol(symbol, arguments);
                pc += 4 + count;
                break;
            }
            case IrOpcode::SemanticEval: case IrOpcode::SsmProcess: {
                const auto semanticSteps = semanticContext.sharedSemanticSteps
                    ? *semanticContext.sharedSemanticSteps : semanticContext.semanticSteps;
                if (semanticSteps >= semanticContext.maximumSemanticSteps) {
                    throw IrError("IR semantic operation exceeds execution state limit");
                }
                auto* model = runtime.runtimeStateModel();
                if (!model) throw IrError("IR semantic operation requires a RuntimeStateModel");
                const auto destination = ir.words.at(pc + 1);
                const auto symbol = ir.symbols.at(ir.words.at(pc + 2));
                const auto count = ir.words.at(pc + 3);
                std::vector<VmValue> inputs;
                inputs.reserve(count);
                for (std::size_t index = 0; index < count; ++index) {
                    inputs.push_back(registers_.at(ir.words.at(pc + 4 + index)));
                }
                if (semanticContext.sharedSemanticSteps) {
                    semanticContext.semanticSteps = ++*semanticContext.sharedSemanticSteps;
                } else {
                    ++semanticContext.semanticSteps;
                }
                auto result = model->evaluate(RuntimeOperation{symbol}, inputs, semanticContext);
                if (!runtime.validateSemanticResult(result, semanticContext)) {
                    throw IrError("RuntimeStateModel returned an invalid runtime value");
                }
                if (const auto fact = std::get_if<VmFactPtr>(&result); fact && *fact) {
                    (*fact)->origin = VmFact::Origin::Derived;
                    runtime.retainFact(*fact);
                }
                runtime.recordTrace(VmTraceKind::SsmProposal, symbol);
                registers_.at(destination) = std::move(result);
                pc += 4 + count;
                break;
            }
            case IrOpcode::CallNamed: {
                const auto destination = ir.words.at(pc + 1);
                const auto symbol = ir.symbols.at(ir.words.at(pc + 2));
                const auto count = ir.words.at(pc + 3);
                std::vector<VmCallArgument> arguments;
                arguments.reserve(count);
                for (std::size_t index = 0; index < count; ++index) {
                    const auto encodedName = ir.words.at(pc + 4 + 2 * index);
                    arguments.push_back(VmCallArgument{
                        encodedName == 0 ? std::nullopt : std::optional<IrSymbolRef>(
                            ir.symbols.at(encodedName - 1)),
                        registers_.at(ir.words.at(pc + 5 + 2 * index))});
                }
                registers_.at(destination) = runtime.callSymbolNamed(symbol, arguments);
                pc += dynamicWidth(ir, pc, 2);
                break;
            }
            case IrOpcode::MakeArray: {
                const auto destination = ir.words.at(pc + 1);
                const auto count = ir.words.at(pc + 3);
                auto array = std::make_shared<VmArray>();
                array->values.reserve(count);
                for (std::size_t index = 0; index < count; ++index) {
                    array->values.push_back(registers_.at(ir.words.at(pc + 4 + index)));
                }
                registers_.at(destination) = std::move(array);
                pc += 4 + count;
                break;
            }
            case IrOpcode::MakeMap: {
                const auto destination = ir.words.at(pc + 1);
                const auto count = ir.words.at(pc + 3);
                auto map = std::make_shared<VmMap>();
                map->entries.reserve(count);
                for (std::size_t index = 0; index < count; ++index) {
                    const auto symbol = ir.symbols.at(ir.words.at(pc + 4 + 2 * index));
                    map->entries.emplace_back(symbol, registers_.at(ir.words.at(pc + 5 + 2 * index)));
                }
                registers_.at(destination) = std::move(map);
                pc += 4 + 2 * count;
                break;
            }
            case IrOpcode::MakeFact: {
                auto fact = std::make_shared<VmFact>();
                fact->type = ir.symbols.at(ir.words.at(pc + 2));
                runtime.retainFact(fact);
                registers_.at(ir.words.at(pc + 1)) = std::move(fact);
                pc += 3;
                break;
            }
            case IrOpcode::GetField: {
                const auto destination = ir.words.at(pc + 1);
                const auto target = ir.words.at(pc + 2);
                const auto field = ir.symbols.at(ir.words.at(pc + 3));
                const auto read = [&](const auto& entries) -> VmValue {
                    for (const auto& [existingField, existingValue] : entries) {
                        if (existingField == field) return existingValue;
                    }
                    throw IrError("IR field is not present on this value");
                };
                if (const auto fact = std::get_if<VmFactPtr>(&registers_.at(target)); fact && *fact) {
                    registers_.at(destination) = read((*fact)->fields);
                } else if (const auto map = std::get_if<VmMapPtr>(&registers_.at(target)); map && *map) {
                    registers_.at(destination) = read((*map)->entries);
                } else {
                    throw IrError("IR field access requires a fact or map value");
                }
                pc += 4;
                break;
            }
            case IrOpcode::SetField: {
                const auto object = ir.words.at(pc + 1);
                const auto field = ir.symbols.at(ir.words.at(pc + 2));
                const auto value = registers_.at(ir.words.at(pc + 3));
                const auto assign = [&](auto& entries) {
                    for (auto& [existingField, existingValue] : entries) {
                        if (existingField == field) {
                            existingValue = value;
                            return;
                        }
                    }
                    entries.emplace_back(field, value);
                };
                if (const auto fact = std::get_if<VmFactPtr>(&registers_.at(object)); fact && *fact) {
                    runtime.mutateFact(*fact, field, value);
                } else if (const auto map = std::get_if<VmMapPtr>(&registers_.at(object)); map && *map) {
                    assign((*map)->entries);
                } else {
                    throw IrError("IR field assignment requires a fact or map value");
                }
                pc += 4;
                break;
            }
            case IrOpcode::Move:
                registers_.at(ir.words.at(pc + 1)) = registers_.at(ir.words.at(pc + 2));
                pc += 3;
                break;
            case IrOpcode::ForEachFact: {
                const auto type = ir.symbols.at(ir.words.at(pc + 2));
                const auto callback = ir.symbols.at(ir.words.at(pc + 3));
                const auto snapshot = runtime.snapshotFacts(type);
                auto results = std::make_shared<VmArray>();
                results->values.reserve(snapshot.size());
                for (const auto& fact : snapshot) {
                    const VmValue argument = fact;
                    results->values.push_back(runtime.callSymbol(callback, std::span<const VmValue>(&argument, 1)));
                }
                registers_.at(ir.words.at(pc + 1)) = std::move(results);
                pc += 4;
                break;
            }
            case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div: case IrOpcode::Mod: {
                const auto lhs = std::get_if<double>(&registers_.at(ir.words.at(pc + 2)));
                const auto rhs = std::get_if<double>(&registers_.at(ir.words.at(pc + 3)));
                if (!lhs || !rhs) throw IrError("IR arithmetic operands must be numbers");
                double value = 0.0;
                if (op == IrOpcode::Add) value = *lhs + *rhs;
                else if (op == IrOpcode::Sub) value = *lhs - *rhs;
                else if (op == IrOpcode::Mul) value = *lhs * *rhs;
                else if (op == IrOpcode::Div) { if (*rhs == 0.0) throw IrError("IR division by zero"); value = *lhs / *rhs; }
                else { if (*rhs == 0.0) throw IrError("IR modulo by zero"); value = std::fmod(*lhs, *rhs); }
                registers_.at(ir.words.at(pc + 1)) = value;
                pc += 4;
                break;
            }
            case IrOpcode::Similarity:
                registers_.at(ir.words.at(pc + 1)) = VmDegree(similarityDegree(registers_.at(ir.words.at(pc + 2)), registers_.at(ir.words.at(pc + 3))));
                pc += 4;
                break;
            case IrOpcode::Membership: {
                const auto number = [&](std::size_t offset) {
                    const auto result = std::get_if<double>(&registers_.at(ir.words.at(pc + offset)));
                    if (!result) throw IrError("IR membership operands must be numbers");
                    return *result;
                };
                registers_.at(ir.words.at(pc + 1)) = VmDegree(VmFactStore::gaussianMembership(number(2), {number(3), number(4), number(5)}));
                pc += 6;
                break;
            }
            case IrOpcode::Compare:
                {
                    const auto& lhs = registers_.at(ir.words.at(pc + 2));
                    const auto& rhs = registers_.at(ir.words.at(pc + 3));
                    const auto kind = static_cast<IrComparison>(ir.words.at(pc + 4));
                    bool result = false;
                    if (kind == IrComparison::Equal || kind == IrComparison::NotEqual) {
                        result = vmValuesEqual(lhs, rhs);
                        if (kind == IrComparison::NotEqual) result = !result;
                    } else {
                        const auto numeric = [](const VmValue& value) -> std::optional<double> {
                            if (const auto number = std::get_if<double>(&value)) return *number;
                            if (const auto degree = std::get_if<VmDegree>(&value)) return degree->value;
                            return std::nullopt;
                        };
                        const auto leftNumber = numeric(lhs); const auto rightNumber = numeric(rhs);
                        if (!leftNumber || !rightNumber) throw IrError("ordered IR comparison operands must be numbers or Degrees");
                        if (kind == IrComparison::Less) result = *leftNumber < *rightNumber;
                        else if (kind == IrComparison::LessEqual) result = *leftNumber <= *rightNumber;
                        else if (kind == IrComparison::Greater) result = *leftNumber > *rightNumber;
                        else result = *leftNumber >= *rightNumber;
                    }
                    registers_.at(ir.words.at(pc + 1)) = result;
                }
                pc += 5;
                break;
            case IrOpcode::Jump:
                pc = ir.words.at(pc + 1);
                break;
            case IrOpcode::JumpIfFalse: {
                pc = runtime.shouldBranchFalse(registers_.at(ir.words.at(pc + 1)))
                    ? ir.words.at(pc + 2) : pc + 3;
                break;
            }
            case IrOpcode::Return:
                return registers_.at(ir.words.at(pc + 1));
            default:
                throw IrError("IR opcode is not executable by this VM build");
        }
    }
    throw IrError("IR ended unexpectedly");
}

} // namespace Felidae
