#include "IrModule.h"

#include <unordered_map>
#include <unordered_set>
#include <set>

namespace Felidae {

std::size_t compilerInstructionWidth(const FelidaeIr& ir,std::size_t pc){
    if(pc>=ir.words.size()||ir.words[pc]>=kIrOpcodeCount)throw IrError("compiler IR contains an invalid opcode");
    const auto require=[&](std::size_t width){if(width>ir.words.size()-pc)throw IrError("compiler IR instruction is incomplete");return width;};
    const auto op=static_cast<IrOpcode>(ir.words[pc]);
    switch(op){
    case IrOpcode::End:return require(1);case IrOpcode::Jump:return require(2);
    case IrOpcode::LoadConst:case IrOpcode::LoadSymbol:case IrOpcode::StoreSymbol:case IrOpcode::Move:case IrOpcode::JumpIfFalse:case IrOpcode::CallNative:case IrOpcode::MakeFact:case IrOpcode::Return:return require(3);
    case IrOpcode::ForEachFact:case IrOpcode::Add:case IrOpcode::Sub:case IrOpcode::Mul:case IrOpcode::Div:case IrOpcode::Mod:case IrOpcode::GetField:case IrOpcode::SetField:case IrOpcode::Similarity:
    case IrOpcode::HierarchyIsA:case IrOpcode::HierarchyCommonAncestors:case IrOpcode::HierarchyLeastCommonAncestors:case IrOpcode::HierarchyMostGeneralAncestors:case IrOpcode::TemporalRank:return require(4);
    case IrOpcode::Compare:return require(5);case IrOpcode::Membership:return require(6);
    case IrOpcode::Call:case IrOpcode::SemanticEval:case IrOpcode::MakeArray:case IrOpcode::CallNamed:case IrOpcode::MakeMap:{require(4);const auto count=ir.words[pc+3];const auto stride=op==IrOpcode::CallNamed||op==IrOpcode::MakeMap?2u:1u;if(count>(ir.words.size()-pc-4)/stride)throw IrError("compiler IR dynamic instruction is incomplete");return 4+count*stride;}
    case IrOpcode::Count:break;
    }throw IrError("compiler IR contains an invalid opcode");
}

void verifyIrModule(const IrModule& module){
    if (!module.symbolTable.empty()) {
        if (module.sentencePieceModelHash.empty()) throw IrError("IR module has no SentencePiece model identity");
        std::set<PieceSequence> uniqueSymbols;
        for (const auto& pieces : module.symbolTable) {
            if (pieces.empty() || !uniqueSymbols.insert(pieces).second) {
                throw IrError("IR module symbol table is empty or duplicated");
            }
        }
        const auto requireSymbol = [&](IrSymbolRef symbol) {
            if (symbol == 0 || symbol > module.symbolTable.size()) {
                throw IrError("IR module symbol index is outside its PieceId table");
            }
        };
        const auto verifyProgramSymbols = [&](const FelidaeIr& ir) {
            for (const auto symbol : ir.symbols) requireSymbol(symbol);
        };
        verifyProgramSymbols(module.ir);
        requireSymbol(module.entryProcedure);
        for (const auto& [symbol, procedure] : module.procedures) {
            requireSymbol(symbol);
            verifyProgramSymbols(procedure.ir);
            for (const auto parameter : procedure.positionalParameters) requireSymbol(parameter);
            for (const auto parameter : procedure.namedParameters) requireSymbol(parameter);
        }
        for (const auto& type : module.factTypes) {
            requireSymbol(type.symbol);
            for (const auto parent : type.parents) requireSymbol(parent);
        }
    }
    IrVerifier::verify(module.ir);
    if(module.entryProcedure==0||!module.procedures.contains(module.entryProcedure))throw IrError("compiler IR entry procedure is invalid");
    const auto verifyCalls=[&](const FelidaeIr& ir){for(std::size_t pc=0;pc<ir.words.size();){const auto opcode=static_cast<IrOpcode>(ir.words[pc]);if(opcode==IrOpcode::Call||opcode==IrOpcode::CallNamed||opcode==IrOpcode::ForEachFact){const auto operand=opcode==IrOpcode::ForEachFact?3u:2u;const auto index=static_cast<std::size_t>(ir.words[pc+operand]);if(index>=ir.symbols.size()||!module.procedures.contains(ir.symbols[index]))throw IrError("compiler IR call references an unknown procedure");}pc+=compilerInstructionWidth(ir,pc);}};
    verifyCalls(module.ir);
    for(const auto& [symbol,procedure]:module.procedures){if(symbol==0)throw IrError("compiler IR procedure symbol is invalid");if(procedure.positionalParameters.size()!=procedure.namedParameters.size())throw IrError("compiler IR procedure parameter metadata is inconsistent");std::unordered_set<IrSymbolRef> positional,named;for(const auto parameter:procedure.positionalParameters)if(parameter==0||!positional.insert(parameter).second)throw IrError("compiler IR positional parameter metadata is invalid");for(const auto parameter:procedure.namedParameters)if(parameter==0||!named.insert(parameter).second)throw IrError("compiler IR named parameter metadata is invalid");IrVerifier::verify(procedure.ir);verifyCalls(procedure.ir);}
    std::unordered_map<IrSymbolRef,const IrFactType*> types;for(const auto& type:module.factTypes)if(type.symbol==0||!types.emplace(type.symbol,&type).second)throw IrError("compiler IR fact type is invalid or duplicated");for(const auto& type:module.factTypes){std::unordered_set<IrSymbolRef> parents;for(const auto parent:type.parents)if(parent==0||parent==type.symbol||!types.contains(parent)||!parents.insert(parent).second)throw IrError("compiler IR hierarchy parent is invalid");}
    std::unordered_set<IrSymbolRef> visiting,visited;const auto visit=[&](auto&& self,IrSymbolRef type)->void{if(visited.contains(type))return;if(!visiting.insert(type).second)throw IrError("compiler IR hierarchy contains a cycle");for(const auto parent:types.at(type)->parents)self(self,parent);visiting.erase(type);visited.insert(type);};for(const auto& [symbol,_]:types)visit(visit,symbol);
}

VerifiedIrModule verifyIrModule(IrModule&& module) {
    verifyIrModule(static_cast<const IrModule&>(module));
    return VerifiedIrModule(std::move(module));
}

} // namespace Felidae
