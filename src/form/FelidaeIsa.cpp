#include "IsaLowerer.h"
#ifdef FELIDAE_LOWERER_ONLY
#include "FelidaeAssembler.h"
#include "../Symbol.h"
#endif

#include <algorithm>
#include <deque>
#include <limits>
#include <optional>
#include <unordered_set>

namespace Felidae {
namespace {

#ifdef FELIDAE_LOWERER_ONLY
std::uint8_t narrowRegister(IrWord value) {
    if (value >= kIsaRegisterLimit) throw IrError("compiler IR register exceeds ISA v1 range");
    return static_cast<std::uint8_t>(value);
}

std::uint16_t narrowIndex(IrWord value, const char* kind) {
    if (value >= kIsaShortIndexLimit) throw IrError(std::string("compiler IR ") + kind + " exceeds ISA v1 range");
    return static_cast<std::uint16_t>(value);
}

std::size_t irWidth(const FelidaeIr& ir, std::size_t pc) {
    if (pc >= ir.words.size() || ir.words[pc] >= kIrOpcodeCount) throw IrError("compiler IR contains an unknown opcode");
    const auto fixed = [&](std::size_t width) {
        if (width > ir.words.size() - pc) throw IrError("compiler IR instruction is incomplete");
        return width;
    };
    const auto op = static_cast<IrOpcode>(ir.words[pc]);
    switch (op) {
    case IrOpcode::End: return fixed(1);
    case IrOpcode::Jump: return fixed(2);
    case IrOpcode::LoadConst: case IrOpcode::LoadSymbol: case IrOpcode::StoreSymbol:
    case IrOpcode::Move: case IrOpcode::JumpIfFalse: case IrOpcode::CallNative:
    case IrOpcode::MakeFact: case IrOpcode::Return: return fixed(3);
    case IrOpcode::ForEachFact: case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul:
    case IrOpcode::Div: case IrOpcode::Mod: case IrOpcode::GetField: case IrOpcode::SetField:
    case IrOpcode::Similarity: case IrOpcode::HierarchyIsA: case IrOpcode::HierarchyCommonAncestors:
    case IrOpcode::HierarchyLeastCommonAncestors:
    case IrOpcode::HierarchyMostGeneralAncestors: case IrOpcode::TemporalRank: return fixed(4);
    case IrOpcode::Compare: return fixed(5);
    case IrOpcode::Membership: return fixed(6);
    case IrOpcode::Call: case IrOpcode::SemanticEval:
    case IrOpcode::MakeArray: case IrOpcode::CallNamed: case IrOpcode::MakeMap: {
        fixed(4);
        const auto count = ir.words[pc + 3];
        const auto stride = op == IrOpcode::CallNamed || op == IrOpcode::MakeMap ? 2u : 1u;
        if (count > (ir.words.size() - pc - 4) / stride) throw IrError("compiler IR dynamic instruction is incomplete");
        return 4 + count * stride;
    }
    case IrOpcode::Count: break;
    }
    throw IrError("compiler IR contains an unknown opcode");
}

IsaOpcode comparisonOpcode(IrWord comparison) {
    switch (static_cast<IrComparison>(comparison)) {
    case IrComparison::Equal: return IsaOpcode::CompareEqual;
    case IrComparison::NotEqual: return IsaOpcode::CompareNotEqual;
    case IrComparison::Less: return IsaOpcode::CompareLess;
    case IrComparison::LessEqual: return IsaOpcode::CompareLessEqual;
    case IrComparison::Greater: return IsaOpcode::CompareGreater;
    case IrComparison::GreaterEqual: return IsaOpcode::CompareGreaterEqual;
    }
    throw IrError("compiler IR comparison ID is invalid");
}
#else
void requireWords(std::span<const IsaWord> words, std::size_t pc, std::size_t width) {
    if (pc > words.size() || width > words.size() - pc) throw IrError("ISA instruction is incomplete");
}

std::size_t packedRegisterWords(std::size_t count) { return (count + 3u) / 4u; }

bool validSymbolDisplayName(IrSymbolRef symbol, std::string_view name) {
    if (name.empty() || std::any_of(name.begin(), name.end(), [](char character) {
            const auto byte = static_cast<unsigned char>(character);
            return byte < 0x20u || byte == 0x7fu;
        })) return false;
    switch (symbol) {
    case 1: return name == "__type";
    case 2: return name == "__parent";
    case 3: return name == "__return";
    case 4: return name == "system";
    case 5: return name == "result";
    case 6: return name == "system:result";
    default: break;
    }
    constexpr IrSymbolRef generatedBase = IrSymbolRef{1} << 63u;
    if (symbol >= generatedBase) {
        return name == "$g" + std::to_string(symbol - generatedBase);
    }
    IrSymbolRef hashed = 1469598103934665603ull;
    for (const char character : name) {
        hashed ^= static_cast<unsigned char>(character);
        hashed *= 1099511628211ull;
    }
    hashed &= ~(IrSymbolRef{1} << 63u);
    hashed |= IrSymbolRef{1} << 62u;
    return hashed == symbol;
}
#endif

} // namespace

#ifndef FELIDAE_LOWERER_ONLY
bool isKnownIsaOpcode(std::uint8_t opcode) noexcept {
    switch (static_cast<IsaOpcode>(opcode)) {
    case IsaOpcode::Halt: case IsaOpcode::LoadConstant: case IsaOpcode::LoadGlobal:
    case IsaOpcode::StoreGlobal: case IsaOpcode::Move: case IsaOpcode::Add:
    case IsaOpcode::Subtract: case IsaOpcode::Multiply: case IsaOpcode::Divide:
    case IsaOpcode::Modulo: case IsaOpcode::CompareEqual: case IsaOpcode::CompareNotEqual:
    case IsaOpcode::CompareLess: case IsaOpcode::CompareLessEqual:
    case IsaOpcode::CompareGreater: case IsaOpcode::CompareGreaterEqual:
    case IsaOpcode::BooleanNot: case IsaOpcode::BooleanAnd: case IsaOpcode::BooleanOr:
    case IsaOpcode::Jump: case IsaOpcode::JumpIfFalse: case IsaOpcode::Call:
    case IsaOpcode::CallNative: case IsaOpcode::Return: case IsaOpcode::CallNamed:
    case IsaOpcode::MakeFact:
    case IsaOpcode::GetField: case IsaOpcode::SetField: case IsaOpcode::QueryFacts:
    case IsaOpcode::MakeArray: case IsaOpcode::MakeMap: case IsaOpcode::Similarity:
    case IsaOpcode::Membership: case IsaOpcode::HierarchyIsA:
    case IsaOpcode::HierarchyCommonAncestors: case IsaOpcode::HierarchyLeastCommonAncestors:
    case IsaOpcode::HierarchyMostGeneralAncestors: case IsaOpcode::TemporalRank:
    case IsaOpcode::SemanticEval: return true;
    }
    return false;
}

bool isKnownSemanticOperation(std::uint16_t operation) noexcept {
    switch (static_cast<SemanticOperationId>(operation)) {
    case SemanticOperationId::Identity: case SemanticOperationId::SelectFact:
    case SemanticOperationId::DeriveFact: case SemanticOperationId::EvaluateDegree: return true;
    }
    return false;
}

bool semanticOperationAcceptsArity(std::uint16_t operation,
                                   std::size_t inputCount) noexcept {
    if (!isKnownSemanticOperation(operation)) return false;
    // ISA v1 semantic operations are unary. New arities require an explicit
    // operation ID (or a future ISA version), never model interpretation.
    return inputCount == 1;
}

std::size_t isaInstructionWidth(std::span<const IsaWord> words, std::size_t pc) {
    requireWords(words, pc, 1);
    const auto decoded = decodeIsaWord(words[pc]);
    if (!isKnownIsaOpcode(static_cast<std::uint8_t>(decoded.opcode))) throw IrError("ISA contains an unknown opcode");
    switch (decoded.opcode) {
    case IsaOpcode::Call: case IsaOpcode::SemanticEval:
        return 2 + packedRegisterWords(decoded.b);
    case IsaOpcode::CallNamed: return 2 + decoded.b;
    case IsaOpcode::MakeArray: return 1 + packedRegisterWords(decoded.b);
    case IsaOpcode::MakeMap: return 1 + decoded.b;
    case IsaOpcode::GetField: case IsaOpcode::SetField: case IsaOpcode::QueryFacts:
    case IsaOpcode::Membership: case IsaOpcode::TemporalRank:
        return 2;
    default: return 1;
    }
}

void IsaVerifier::verify(const IsaBlock& block, const IsaVerificationContext& context) {
    if (block.registerCount == 0 || block.registerCount > kIsaRegisterLimit) throw IrError("ISA register count is invalid");
    std::unordered_set<std::size_t> boundaries;
    for (std::size_t pc = 0; pc < block.words.size();) {
        boundaries.insert(pc);
        const auto width = isaInstructionWidth(block.words, pc);
        requireWords(block.words, pc, width);
        pc += width;
    }
    boundaries.insert(block.words.size());
    const auto reg = [&](std::uint8_t value) {
        if (value >= block.registerCount) throw IrError("ISA references an invalid register");
    };
    const auto pool = [](std::size_t value, std::size_t size, const char* kind) {
        if (value >= size) throw IrError(std::string("ISA references an invalid ") + kind + " index");
    };
    const auto requireZero = [](IsaWord value, const char* field) {
        if (value != 0) throw IrError(std::string("ISA ") + field + " reserved bits are nonzero");
    };
    const auto verifyPackedRegisterPadding = [&](std::size_t pc,
                                                  std::size_t firstWord,
                                                  std::size_t count) {
        const auto remainder = count % 4u;
        if (remainder == 0) return;
        const auto word = block.words[pc + firstWord + count / 4u];
        const auto usedBits = static_cast<unsigned>(remainder * 8u);
        requireZero(word >> usedBits, "packed-register");
    };
    bool terminator = false;
    for (std::size_t pc = 0; pc < block.words.size();) {
        const auto d = decodeIsaWord(block.words[pc]);
        const auto width = isaInstructionWidth(block.words, pc);
        switch (d.opcode) {
        case IsaOpcode::Halt:
            requireZero(block.words[pc] >> 8u, "Halt");
            terminator = true;
            break;
        case IsaOpcode::LoadConstant: reg(d.a); pool(d.bx, context.constantCount, "constant"); break;
        case IsaOpcode::LoadGlobal: case IsaOpcode::CallNative: case IsaOpcode::MakeFact:
            reg(d.a); pool(d.bx, context.symbolCount, "symbol"); break;
        case IsaOpcode::StoreGlobal: reg(d.a); pool(d.bx, context.symbolCount, "symbol"); break;
        case IsaOpcode::Move: case IsaOpcode::BooleanNot:
            reg(d.a); reg(d.b); requireZero(d.c, "unary"); break;
        case IsaOpcode::Add: case IsaOpcode::Subtract: case IsaOpcode::Multiply:
        case IsaOpcode::Divide: case IsaOpcode::Modulo: case IsaOpcode::CompareEqual:
        case IsaOpcode::CompareNotEqual: case IsaOpcode::CompareLess:
        case IsaOpcode::CompareLessEqual: case IsaOpcode::CompareGreater:
        case IsaOpcode::CompareGreaterEqual: case IsaOpcode::BooleanAnd:
        case IsaOpcode::BooleanOr: case IsaOpcode::Similarity:
        case IsaOpcode::HierarchyIsA: case IsaOpcode::HierarchyCommonAncestors:
        case IsaOpcode::HierarchyLeastCommonAncestors: case IsaOpcode::HierarchyMostGeneralAncestors:
            reg(d.a); reg(d.b); reg(d.c); break;
        case IsaOpcode::Jump:
            if (!boundaries.contains(d.ax)) throw IrError("ISA jump target is invalid");
            break;
        case IsaOpcode::JumpIfFalse:
            reg(d.a); if (!boundaries.contains(d.bx)) throw IrError("ISA branch target is invalid");
            break;
        case IsaOpcode::Call: {
            requireZero(d.c, "Call");
            requireZero(block.words[pc + 1] >> 16u, "Call procedure");
            reg(d.a); pool(block.words[pc + 1], context.procedureCount, "procedure");
            for (std::size_t i = 0; i < d.b; ++i) reg(static_cast<std::uint8_t>((block.words[pc + 2 + i / 4] >> ((i % 4) * 8u)) & 0xffu));
            verifyPackedRegisterPadding(pc, 2, d.b);
            break;
        }
        case IsaOpcode::CallNamed: {
            requireZero(d.c, "CallNamed");
            requireZero(block.words[pc + 1] >> 16u, "CallNamed procedure");
            reg(d.a); pool(block.words[pc + 1], context.procedureCount, "procedure");
            for (std::size_t i = 0; i < d.b; ++i) {
                const auto argument = block.words[pc + 2 + i];
                const auto name = static_cast<std::uint16_t>(argument & 0xffffu);
                if (name != 0xffffu) pool(name, context.symbolCount, "symbol");
                reg(static_cast<std::uint8_t>((argument >> 16u) & 0xffu));
                if ((argument & 0xff000000u) != 0) throw IrError("ISA named-call reserved bits are nonzero");
            }
            break;
        }
        case IsaOpcode::Return:
            reg(d.a); requireZero(block.words[pc] >> 16u, "Return"); terminator = true; break;
        case IsaOpcode::GetField:
            reg(d.a); reg(d.b); requireZero(d.c, "GetField");
            requireZero(block.words[pc + 1] >> 16u, "GetField symbol");
            pool(block.words[pc + 1], context.symbolCount, "symbol"); break;
        case IsaOpcode::SetField:
            reg(d.a); reg(d.b); requireZero(d.c, "SetField");
            requireZero(block.words[pc + 1] >> 16u, "SetField symbol");
            pool(block.words[pc + 1], context.symbolCount, "symbol"); break;
        case IsaOpcode::QueryFacts:
            reg(d.a); requireZero(d.b, "QueryFacts B"); requireZero(d.c, "QueryFacts C");
            pool(block.words[pc + 1] & 0xffffu, context.symbolCount, "symbol");
            pool(block.words[pc + 1] >> 16u, context.procedureCount, "procedure"); break;
        case IsaOpcode::MakeArray:
            reg(d.a); requireZero(d.c, "MakeArray");
            for (std::size_t i=0;i<d.b;++i) reg(static_cast<std::uint8_t>((block.words[pc+1+i/4]>>((i%4)*8u))&0xffu));
            verifyPackedRegisterPadding(pc, 1, d.b); break;
        case IsaOpcode::MakeMap:
            reg(d.a); requireZero(d.c, "MakeMap");
            for (std::size_t i=0;i<d.b;++i) { const auto entry=block.words[pc+1+i]; pool(entry&0xffffu,context.symbolCount,"symbol"); reg(static_cast<std::uint8_t>((entry>>16u)&0xffu)); if((entry&0xff000000u)!=0)throw IrError("ISA map reserved bits are nonzero"); } break;
        case IsaOpcode::Membership:
            reg(d.a); reg(d.b); reg(d.c); { const auto tail=block.words[pc+1]; reg(static_cast<std::uint8_t>(tail&0xffu)); reg(static_cast<std::uint8_t>((tail>>8u)&0xffu)); if((tail&0xffff0000u)!=0)throw IrError("ISA membership reserved bits are nonzero"); } break;
        case IsaOpcode::TemporalRank:{reg(d.a);requireZero(d.b,"TemporalRank B");requireZero(d.c,"TemporalRank C");const auto fields=block.words[pc+1];pool(fields&0xffffu,context.symbolCount,"symbol");pool(fields>>16u,context.symbolCount,"symbol");break;}
        case IsaOpcode::SemanticEval: {
            requireZero(d.c, "SemanticEval");
            reg(d.a);
            requireZero(block.words[pc + 1] >> 16u, "SemanticEval operation");
            const auto operation = static_cast<std::uint16_t>(block.words[pc + 1]);
            if (!isKnownSemanticOperation(operation)) throw IrError("ISA semantic operation ID is invalid");
            if (!semanticOperationAcceptsArity(operation, d.b)) {
                throw IrError("ISA semantic operation arity is invalid");
            }
            for (std::size_t i = 0; i < d.b; ++i) reg(static_cast<std::uint8_t>((block.words[pc + 2 + i / 4] >> ((i % 4) * 8u)) & 0xffu));
            verifyPackedRegisterPadding(pc, 2, d.b);
            break;
        }
        }
        pc += width;
    }
    if (!terminator) throw IrError("ISA block has no terminating instruction");

    std::vector<std::optional<std::vector<bool>>> incoming(block.words.size());
    std::deque<std::size_t> pending;
    incoming[0]=std::vector<bool>(block.registerCount,false);
    pending.push_back(0);
    const auto enqueue=[&](std::size_t target,const std::vector<bool>& state){
        if(target==block.words.size())throw IrError("ISA control flow can fall past the end of a block");
        auto& existing=incoming.at(target);
        if(!existing){existing=state;pending.push_back(target);return;}
        auto merged=*existing;bool changed=false;
        for(std::size_t index=0;index<merged.size();++index){const bool value=merged[index]&&state[index];changed|=value!=merged[index];merged[index]=value;}
        if(changed){existing=std::move(merged);pending.push_back(target);}
    };
    while(!pending.empty()){
        const auto pc=pending.front();pending.pop_front();auto state=*incoming[pc];const auto d=decodeIsaWord(block.words[pc]);const auto width=isaInstructionWidth(block.words,pc);
        const auto read=[&](std::uint8_t value){if(!state[value])throw IrError("ISA reads an uninitialized register");};
        const auto write=[&](std::uint8_t value){state[value]=true;};
        switch(d.opcode){
        case IsaOpcode::Halt:continue;
        case IsaOpcode::LoadConstant:case IsaOpcode::LoadGlobal:case IsaOpcode::CallNative:case IsaOpcode::MakeFact:case IsaOpcode::QueryFacts:case IsaOpcode::TemporalRank:write(d.a);break;
        case IsaOpcode::StoreGlobal:read(d.a);break;
        case IsaOpcode::Move:case IsaOpcode::BooleanNot:read(d.b);write(d.a);break;
        case IsaOpcode::Add:case IsaOpcode::Subtract:case IsaOpcode::Multiply:case IsaOpcode::Divide:case IsaOpcode::Modulo:case IsaOpcode::CompareEqual:case IsaOpcode::CompareNotEqual:case IsaOpcode::CompareLess:case IsaOpcode::CompareLessEqual:case IsaOpcode::CompareGreater:case IsaOpcode::CompareGreaterEqual:case IsaOpcode::BooleanAnd:case IsaOpcode::BooleanOr:case IsaOpcode::Similarity:case IsaOpcode::HierarchyIsA:case IsaOpcode::HierarchyCommonAncestors:case IsaOpcode::HierarchyLeastCommonAncestors:case IsaOpcode::HierarchyMostGeneralAncestors:read(d.b);read(d.c);write(d.a);break;
        case IsaOpcode::Jump:enqueue(d.ax,state);continue;
        case IsaOpcode::JumpIfFalse:read(d.a);enqueue(d.bx,state);break;
        case IsaOpcode::Call:for(std::size_t i=0;i<d.b;++i)read(static_cast<std::uint8_t>((block.words[pc+2+i/4]>>((i%4)*8u))&0xffu));write(d.a);break;
        case IsaOpcode::CallNamed:for(std::size_t i=0;i<d.b;++i)read(static_cast<std::uint8_t>(block.words[pc+2+i]>>16u));write(d.a);break;
        case IsaOpcode::Return:read(d.a);continue;
        case IsaOpcode::GetField:read(d.b);write(d.a);break;
        case IsaOpcode::SetField:read(d.a);read(d.b);break;
        case IsaOpcode::MakeArray:for(std::size_t i=0;i<d.b;++i)read(static_cast<std::uint8_t>((block.words[pc+1+i/4]>>((i%4)*8u))&0xffu));write(d.a);break;
        case IsaOpcode::MakeMap:for(std::size_t i=0;i<d.b;++i)read(static_cast<std::uint8_t>(block.words[pc+1+i]>>16u));write(d.a);break;
        case IsaOpcode::Membership:{read(d.b);read(d.c);const auto tail=block.words[pc+1];read(static_cast<std::uint8_t>(tail));read(static_cast<std::uint8_t>(tail>>8u));write(d.a);break;}
        case IsaOpcode::SemanticEval:for(std::size_t i=0;i<d.b;++i)read(static_cast<std::uint8_t>((block.words[pc+2+i/4]>>((i%4)*8u))&0xffu));write(d.a);break;
        }
        enqueue(pc+width,state);
    }
}

void verifyIsaModule(const IsaModule& module) {
    if (module.isaVersion != kFelidaeIsaVersion) throw IrError("Felidae ISA version is unsupported");
    if (module.procedures.empty() || module.procedures.size()!=module.procedureSymbols.size() ||
        module.entryProcedure>=module.procedures.size()) throw IrError("ISA procedure metadata is invalid");
    std::unordered_set<IrSymbolRef> symbols;
    const auto verifyProgram = [&](const IsaProgram& program) {
        if (!program.constantKinds.empty() &&
            program.constantKinds.size() != program.constants.size()) {
            throw IrError("ISA constant kinds do not match its constant pool");
        }
        for (std::size_t index = 0; index < program.constants.size(); ++index) {
            const auto kind = program.constantKinds.empty()
                ? IrConstantKind::Number : program.constantKinds[index];
            if (kind > IrConstantKind::Text) {
                throw IrError("ISA constant kind is invalid");
            }
            if (kind == IrConstantKind::Text &&
                program.constants[index] >= program.texts.size()) {
                throw IrError("ISA text constant index is invalid");
            }
            if (kind == IrConstantKind::Boolean && program.constants[index] > 1) {
                throw IrError("ISA boolean constant must be numeric 0.0 or 1.0");
            }
            if (kind == IrConstantKind::Nil && program.constants[index] != 0) {
                throw IrError("ISA nil constant payload must be zero");
            }
        }
        for (const auto symbol : program.symbols) {
            if (symbol == 0) throw IrError("ISA symbol pool contains an invalid symbol");
        }
        IsaVerifier::verify(program.code,{program.constants.size(),program.symbols.size(),module.procedures.size()});
        std::unordered_set<std::size_t> boundaries;
        for(std::size_t pc=0;pc<program.code.words.size();pc+=isaInstructionWidth(program.code.words,pc))boundaries.insert(pc);
        boundaries.insert(program.code.words.size());
        for(const auto& entry:program.sourceMap){
            if(!boundaries.contains(entry.instructionWord))throw IrError("ISA source map does not reference an instruction boundary");
            const auto& span=entry.sourceSpan;
            if(span.startLine<=0||span.startColumn<=0||span.endLine<span.startLine||
               (span.endLine==span.startLine&&span.endColumn<span.startColumn))throw IrError("ISA source map contains an invalid span");
        }
    };
    for (std::size_t index=0;index<module.procedures.size();++index) {
        if (module.procedureSymbols[index]==0 || !symbols.insert(module.procedureSymbols[index]).second) throw IrError("ISA procedure symbol is invalid");
        const auto& procedure=module.procedures[index];
        if(procedure.positionalParameters.size()!=procedure.namedParameters.size())throw IrError("ISA procedure parameter metadata is inconsistent");
        verifyProgram(procedure.program);
    }
    verifyProgram(module.initializer);
    for(const auto& type:module.factTypes){if(type.symbol==0)throw IrError("ISA fact type symbol is invalid");for(const auto parent:type.parents)if(parent==0)throw IrError("ISA fact parent symbol is invalid");}
    IrSymbolRef previousName = 0;
    for (const auto& entry : module.symbolNames) {
        if (entry.symbol == 0 || entry.symbol <= previousName ||
            !validSymbolDisplayName(entry.symbol, entry.name)) {
            throw IrError("ISA symbol display table is invalid or noncanonical");
        }
        previousName = entry.symbol;
    }
}
#else

IsaBlock IsaLowerer::lower(const FelidaeIr& ir,
                           const std::unordered_map<IrSymbolRef, std::uint16_t>& procedures) {
    IrVerifier::verify(ir);
    if (ir.registerCount == 0 || ir.registerCount > kIsaRegisterLimit) throw IrError("compiler IR register count exceeds ISA v1 range");
    IsaBlock result;
    result.registerCount = static_cast<std::uint16_t>(ir.registerCount);
    FelidaeAssembler assembler;
    std::unordered_map<std::size_t,std::size_t> isaOffsets;
    std::unordered_map<std::size_t,FelidaeAssembler::Label> labels;
    for(std::size_t irPc=0;irPc<ir.words.size();irPc+=irWidth(ir,irPc))labels.emplace(irPc,assembler.createLabel());
    labels.emplace(ir.words.size(),assembler.createLabel());
    const auto target=[&](IrWord value){const auto found=labels.find(value);if(found==labels.end())throw IrError("compiler IR branch target is not an instruction boundary");return found->second;};
    for (std::size_t pc = 0; pc < ir.words.size();) {
        assembler.bind(labels.at(pc));
        isaOffsets.emplace(pc,assembler.wordOffset());
        const auto op = static_cast<IrOpcode>(ir.words[pc]);
        switch (op) {
        case IrOpcode::End: assembler.halt(); break;
        case IrOpcode::LoadConst: assembler.loadConstant(narrowRegister(ir.words[pc+1]),narrowIndex(ir.words[pc+2],"constant index")); break;
        case IrOpcode::LoadSymbol: assembler.loadGlobal(narrowRegister(ir.words[pc+1]),narrowIndex(ir.words[pc+2],"symbol index")); break;
        case IrOpcode::StoreSymbol: assembler.storeGlobal(narrowIndex(ir.words[pc+1],"symbol index"),narrowRegister(ir.words[pc+2])); break;
        case IrOpcode::Move: assembler.move(narrowRegister(ir.words[pc+1]),narrowRegister(ir.words[pc+2])); break;
        case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul: case IrOpcode::Div: case IrOpcode::Mod:
        case IrOpcode::Similarity: case IrOpcode::HierarchyIsA: case IrOpcode::HierarchyCommonAncestors:
        case IrOpcode::HierarchyLeastCommonAncestors: case IrOpcode::HierarchyMostGeneralAncestors: {
            const auto isa = op == IrOpcode::Add ? IsaOpcode::Add :
                op == IrOpcode::Sub ? IsaOpcode::Subtract :
                op == IrOpcode::Mul ? IsaOpcode::Multiply :
                op == IrOpcode::Div ? IsaOpcode::Divide :
                op == IrOpcode::Mod ? IsaOpcode::Modulo :
                op == IrOpcode::Similarity ? IsaOpcode::Similarity :
                op == IrOpcode::HierarchyIsA ? IsaOpcode::HierarchyIsA :
                op == IrOpcode::HierarchyCommonAncestors ? IsaOpcode::HierarchyCommonAncestors :
                op == IrOpcode::HierarchyLeastCommonAncestors ? IsaOpcode::HierarchyLeastCommonAncestors :
                IsaOpcode::HierarchyMostGeneralAncestors;
            assembler.binary(isa,narrowRegister(ir.words[pc+1]),narrowRegister(ir.words[pc+2]),narrowRegister(ir.words[pc+3])); break;
        }
        case IrOpcode::TemporalRank:
            assembler.temporalRank(narrowRegister(ir.words[pc+1]),
                narrowIndex(ir.words[pc+2],"effective-at symbol index"),
                narrowIndex(ir.words[pc+3],"priority symbol index"));
            break;
        case IrOpcode::Compare: assembler.binary(comparisonOpcode(ir.words[pc+4]),narrowRegister(ir.words[pc+1]),narrowRegister(ir.words[pc+2]),narrowRegister(ir.words[pc+3])); break;
        case IrOpcode::Jump: assembler.jump(target(ir.words[pc+1])); break;
        case IrOpcode::JumpIfFalse: assembler.jumpIfFalse(narrowRegister(ir.words[pc+1]),target(ir.words[pc+2])); break;
        case IrOpcode::Call: {
            const auto count = ir.words[pc+3]; if (count > 255) throw IrError("compiler IR call argument count exceeds ISA v1 range");
            const auto symbolIndex = static_cast<std::size_t>(ir.words[pc+2]);
            if (symbolIndex >= ir.symbols.size()) throw IrError("compiler IR call symbol index is invalid");
            const auto procedure = procedures.find(ir.symbols[symbolIndex]);
            if (procedure == procedures.end()) throw IrError("compiler IR call target is not a procedure");
            std::vector<IsaRegister> arguments;arguments.reserve(count);for(std::size_t i=0;i<count;++i)arguments.push_back(narrowRegister(ir.words[pc+4+i]));
            assembler.call(narrowRegister(ir.words[pc+1]),procedure->second,arguments);
            break;
        }
        case IrOpcode::CallNative: assembler.callNative(narrowRegister(ir.words[pc+1]),narrowIndex(ir.words[pc+2],"symbol index")); break;
        case IrOpcode::CallNamed: {
            const auto count=ir.words[pc+3]; if(count>255)throw IrError("compiler IR named-call argument count exceeds ISA v1 range");
            const auto symbolIndex=static_cast<std::size_t>(ir.words[pc+2]); if(symbolIndex>=ir.symbols.size())throw IrError("compiler IR named-call symbol index is invalid");
            const auto procedure=procedures.find(ir.symbols[symbolIndex]); if(procedure==procedures.end())throw IrError("compiler IR named-call target is not a procedure");
            std::vector<FelidaeAssembler::NamedRegister> arguments;arguments.reserve(count);for(std::size_t i=0;i<count;++i){const auto name=ir.words[pc+4+2*i];const auto encodedName=name==0?std::numeric_limits<std::uint16_t>::max():narrowIndex(name-1,"symbol index");arguments.push_back({encodedName,narrowRegister(ir.words[pc+5+2*i])});}assembler.callNamed(narrowRegister(ir.words[pc+1]),procedure->second,arguments);
            break;
        }
        case IrOpcode::Return: assembler.returnValue(narrowRegister(ir.words[pc+1])); break;
        case IrOpcode::MakeFact: assembler.makeFact(narrowRegister(ir.words[pc+1]),narrowIndex(ir.words[pc+2],"symbol index")); break;
        case IrOpcode::GetField: assembler.getField(narrowRegister(ir.words[pc+1]),narrowRegister(ir.words[pc+2]),narrowIndex(ir.words[pc+3],"symbol index")); break;
        case IrOpcode::SetField: assembler.setField(narrowRegister(ir.words[pc+1]),narrowIndex(ir.words[pc+2],"symbol index"),narrowRegister(ir.words[pc+3])); break;
        case IrOpcode::ForEachFact: {
            const auto symbolIndex = static_cast<std::size_t>(ir.words[pc+3]);
            if (symbolIndex >= ir.symbols.size()) throw IrError("compiler IR query callback index is invalid");
            const auto procedure = procedures.find(ir.symbols[symbolIndex]);
            if (procedure == procedures.end()) throw IrError("compiler IR query callback is not a procedure");
            assembler.queryFacts(narrowRegister(ir.words[pc+1]),narrowIndex(ir.words[pc+2],"symbol index"),procedure->second);break;
        }
        case IrOpcode::MakeArray: {
            const auto count=ir.words[pc+3];std::vector<IsaRegister> values;values.reserve(count);for(std::size_t i=0;i<count;++i)values.push_back(narrowRegister(ir.words[pc+4+i]));assembler.makeArray(narrowRegister(ir.words[pc+1]),values);break;
        }
        case IrOpcode::MakeMap: {
            const auto count=ir.words[pc+3];std::vector<FelidaeAssembler::MapRegister> values;values.reserve(count);for(std::size_t i=0;i<count;++i)values.push_back({narrowIndex(ir.words[pc+4+2*i],"symbol index"),narrowRegister(ir.words[pc+5+2*i])});assembler.makeMap(narrowRegister(ir.words[pc+1]),values);break;
        }
        case IrOpcode::Membership:
            assembler.membership(narrowRegister(ir.words[pc+1]),narrowRegister(ir.words[pc+2]),narrowRegister(ir.words[pc+3]),narrowRegister(ir.words[pc+4]),narrowRegister(ir.words[pc+5]));break;
        case IrOpcode::SemanticEval: {
            const auto operation = ir.words[pc + 2];
            if (operation > std::numeric_limits<std::uint16_t>::max() ||
                !isKnownSemanticOperation(static_cast<std::uint16_t>(operation))) {
                throw IrError("compiler IR semantic operation ID is invalid");
            }
            const auto count = ir.words[pc + 3];
            std::vector<IsaRegister> inputs;
            inputs.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                inputs.push_back(narrowRegister(ir.words[pc + 4 + index]));
            }
            assembler.semanticEval(narrowRegister(ir.words[pc + 1]),
                                   static_cast<SemanticOperationId>(operation), inputs);
            break;
        }
        case IrOpcode::Count: throw IrError("compiler IR contains an unknown opcode");
        }
        pc += irWidth(ir, pc);
    }
    assembler.bind(labels.at(ir.words.size()));
    isaOffsets.emplace(ir.words.size(),assembler.wordOffset());
    result.words = std::move(assembler).finish();
    result.sourceMap.reserve(ir.sourceMap.size());
    for (const auto& entry : ir.sourceMap) {
        const auto offset = isaOffsets.find(entry.instructionWord);
        if (offset == isaOffsets.end()) throw IrError("compiler IR source map cannot be lowered to ISA");
        result.sourceMap.push_back({offset->second,entry.sourceSpan});
    }
    return result;
}

IsaModule IsaLowerer::lowerModule(const IrModule& module) {
    verifyIrModule(module);
    if (module.procedures.size() >= kIsaShortIndexLimit) throw IrError("compiler IR procedure count exceeds ISA v1 range");
    IsaModule result;
    std::vector<std::pair<IrSymbolRef, const IrProcedure*>> sorted;
    sorted.reserve(module.procedures.size());
    for (const auto& [symbol, procedure] : module.procedures) sorted.emplace_back(symbol, &procedure);
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
    std::unordered_map<IrSymbolRef, std::uint16_t> indexes;
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        indexes.emplace(sorted[index].first, static_cast<std::uint16_t>(index));
        result.procedureSymbols.push_back(sorted[index].first);
    }
    const auto entry = indexes.find(module.entryProcedure);
    if (entry == indexes.end()) throw IrError("compiler IR entry procedure is invalid");
    result.entryProcedure = entry->second;
    const auto lowerProgram = [&](const FelidaeIr& ir) {
        IsaProgram program;
        program.code = lower(ir, indexes);
        program.constants = ir.constants;
        program.constantKinds = ir.constantKinds;
        program.texts = ir.texts;
        program.symbols = ir.symbols;
        program.sourceMap = program.code.sourceMap;
        IsaVerifier::verify(program.code, {program.constants.size(), program.symbols.size(), sorted.size()});
        return program;
    };
    result.initializer = lowerProgram(module.ir);
    for (const auto& [_, procedure] : sorted) {
        result.procedures.push_back({lowerProgram(procedure->ir), procedure->positionalParameters,
                                     procedure->namedParameters, procedure->sourceSpan});
    }
    result.factTypes.reserve(module.factTypes.size());
    for (const auto& type : module.factTypes) {
        result.factTypes.push_back({type.symbol, type.parents, type.sourceSpan});
    }
    std::unordered_set<IrSymbolRef> displaySymbols;
    const auto collectProgramSymbols = [&](const IsaProgram& program) {
        displaySymbols.insert(program.symbols.begin(), program.symbols.end());
    };
    collectProgramSymbols(result.initializer);
    for (const auto& procedure : result.procedures) {
        collectProgramSymbols(procedure.program);
        displaySymbols.insert(procedure.positionalParameters.begin(),
                              procedure.positionalParameters.end());
        displaySymbols.insert(procedure.namedParameters.begin(),
                              procedure.namedParameters.end());
    }
    displaySymbols.insert(result.procedureSymbols.begin(), result.procedureSymbols.end());
    for (const auto& type : result.factTypes) {
        displaySymbols.insert(type.symbol);
        displaySymbols.insert(type.parents.begin(), type.parents.end());
    }
    std::vector<IrSymbolRef> orderedDisplaySymbols(displaySymbols.begin(),
                                                   displaySymbols.end());
    std::sort(orderedDisplaySymbols.begin(), orderedDisplaySymbols.end());
    for (const auto symbol : orderedDisplaySymbols) {
        auto name = symbolNameForId(symbol);
        if (!name.empty()) result.symbolNames.push_back({symbol, std::move(name)});
    }
    verifyIsaModule(result);
    return result;
}
#endif

} // namespace Felidae
