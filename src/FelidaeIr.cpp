#include "FelidaeIr.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <limits>
#include <unordered_set>

namespace Felidae {
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
    case IrOpcode::ExecuteProgram: return 2;
    case IrOpcode::LoadSymbol: case IrOpcode::LoadConst: case IrOpcode::Move:
    case IrOpcode::JumpIfFalse: case IrOpcode::CallNative:
    case IrOpcode::MakeFact: case IrOpcode::Return: return 3;
    case IrOpcode::Call: case IrOpcode::CallNamed: case IrOpcode::SemanticEval:
        return 0; // validated below from explicit argument count
    case IrOpcode::MakeArray: return 0; // destination, explicit item count, registers
    case IrOpcode::MakeMap: return 0; // destination, explicit entry count, symbol/register pairs
    case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div: case IrOpcode::Mod:
    case IrOpcode::GetField: case IrOpcode::SetField: return 4;
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
    const auto& leftOpaque = std::get<VmOpaqueValue>(left);
    return leftOpaque == std::get<VmOpaqueValue>(right);
}

bool validVmValue(const VmValue& value, std::size_t depth = 0) {
    constexpr std::size_t kMaximumValueDepth = 256;
    if (depth > kMaximumValueDepth) return false;
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
    if (const auto* opaque = std::get_if<VmOpaqueValue>(&value)) return static_cast<bool>(opaque->object);
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
        const auto next = pc + (op == IrOpcode::Call || op == IrOpcode::SemanticEval || op == IrOpcode::MakeArray
            ? dynamicWidth(ir, pc, 1)
            : op == IrOpcode::CallNamed || op == IrOpcode::MakeMap ? dynamicWidth(ir, pc, 2) : widthFor(op));
        auto write = [&](IrWord target) { state[target] = true; };
        switch (op) {
        case IrOpcode::LoadConst: case IrOpcode::LoadSymbol: write(ir.words[pc + 1]); break;
        case IrOpcode::Move: requireFlowRead(state, ir.words[pc + 2]); write(ir.words[pc + 1]); break;
        case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div: case IrOpcode::Mod:
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
        case IrOpcode::End: case IrOpcode::Jump: case IrOpcode::ExecuteProgram: break;
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
            case IrOpcode::ExecuteProgram:
                requireWords(ir, pc, 2);
                if (ir.words[pc + 1] >= ir.programs.size()) {
                    throw IrError("IR references an invalid program");
                }
                pc += 2;
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
            case IrOpcode::Move:
                requireWords(ir, pc, 3);
                requireRegister(ir, ir.words[pc + 1]);
                requireRegister(ir, ir.words[pc + 2]);
                requireInitialized(initialized, ir.words[pc + 2]);
                initialized[ir.words[pc + 1]] = true;
                pc += 3;
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

VmValue VmRuntime::callSymbol(IrSymbolRef, std::span<const VmValue>) {
    throw IrError("IR calls are unavailable in this runtime");
}

VmValue VmRuntime::callSymbolNamed(IrSymbolRef, std::span<const VmCallArgument>) {
    throw IrError("IR named calls are unavailable in this runtime");
}

VmValue VmRuntime::callNativeSymbol(IrSymbolRef symbol) {
    return callSymbol(symbol, {});
}

RuntimeStateModel* VmRuntime::runtimeStateModel() { return nullptr; }

RuntimeContext VmRuntime::makeRuntimeContext(const FelidaeIr&, const VmValue&) const {
    return {};
}

bool VmRuntime::validateSemanticResult(const VmValue& value, const RuntimeContext&) const {
    return validVmValue(value);
}

VmValue RegisterVm::execute(const FelidaeIr& ir, VmRuntime& runtime, VmValue systemInput) {
    IrVerifier::verify(ir);
    registers_.assign(ir.registerCount, VmNil{});
    auto semanticContext = runtime.makeRuntimeContext(ir, systemInput);
    for (std::size_t pc = 0; pc < ir.words.size();) {
        const auto op = static_cast<IrOpcode>(ir.words[pc]);
        switch (op) {
            case IrOpcode::End:
                throw IrError("IR program completed without a result");
            case IrOpcode::ExecuteProgram: {
                return runtime.executeProgram(ir.words.at(pc + 1), systemInput);
            }
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
                        registers_.at(ir.words.at(pc + 1)) = VmText{{}, ir.texts.at(ir.constants.at(constant))};
                    }
                }
                pc += 3;
                break;
            case IrOpcode::LoadSymbol:
                registers_.at(ir.words.at(pc + 1)) = runtime.loadSymbol(
                    ir.symbols.at(ir.words.at(pc + 2)));
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
            case IrOpcode::SemanticEval: {
                if (semanticContext.semanticSteps >= semanticContext.maximumSemanticSteps) {
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
                ++semanticContext.semanticSteps;
                auto result = model->evaluate(RuntimeOperation{symbol}, inputs, semanticContext);
                if (!runtime.validateSemanticResult(result, semanticContext)) {
                    throw IrError("RuntimeStateModel returned an invalid runtime value");
                }
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
                    assign((*fact)->fields);
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
                        const auto* leftNumber = std::get_if<double>(&lhs);
                        const auto* rightNumber = std::get_if<double>(&rhs);
                        if (!leftNumber || !rightNumber) throw IrError("ordered IR comparison operands must be numbers");
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
