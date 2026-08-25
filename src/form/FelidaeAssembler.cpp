#include "FelidaeAssembler.h"

namespace Felidae {

FelidaeAssembler::Label FelidaeAssembler::createLabel(){if(nextLabel_==0)throw IrError("assembler label space is exhausted");return{nextLabel_++};}
void FelidaeAssembler::bind(Label label){if(label.id==0||label.id>=nextLabel_||!labels_.emplace(label.id,words_.size()).second)throw IrError("assembler label is invalid or already bound");}

void FelidaeAssembler::halt(){emitABC(IsaOpcode::Halt);}
void FelidaeAssembler::loadConstant(IsaRegister d,std::uint16_t c){emitABx(IsaOpcode::LoadConstant,d,c);}
void FelidaeAssembler::loadGlobal(IsaRegister d,std::uint16_t s){emitABx(IsaOpcode::LoadGlobal,d,s);}
void FelidaeAssembler::storeGlobal(std::uint16_t s,IsaRegister r){emitABx(IsaOpcode::StoreGlobal,r,s);}
void FelidaeAssembler::move(IsaRegister d,IsaRegister s){emitABC(IsaOpcode::Move,d,s);}
void FelidaeAssembler::binary(IsaOpcode op,IsaRegister d,IsaRegister l,IsaRegister r){
    switch(op){case IsaOpcode::Add:case IsaOpcode::Subtract:case IsaOpcode::Multiply:case IsaOpcode::Divide:case IsaOpcode::Modulo:case IsaOpcode::CompareEqual:case IsaOpcode::CompareNotEqual:case IsaOpcode::CompareLess:case IsaOpcode::CompareLessEqual:case IsaOpcode::CompareGreater:case IsaOpcode::CompareGreaterEqual:case IsaOpcode::BooleanAnd:case IsaOpcode::BooleanOr:case IsaOpcode::Similarity:case IsaOpcode::HierarchyIsA:case IsaOpcode::HierarchyCommonAncestors:case IsaOpcode::HierarchyLeastCommonAncestors:case IsaOpcode::HierarchyMostGeneralAncestors:emitABC(op,d,l,r);return;default:throw IrError("assembler opcode is not an ABC binary operation");}
}
void FelidaeAssembler::booleanNot(IsaRegister d,IsaRegister s){emitABC(IsaOpcode::BooleanNot,d,s);}
void FelidaeAssembler::jump(Label target){fixups_.push_back({words_.size(),target,IsaOpcode::Jump,0});emitAx(IsaOpcode::Jump,0);}
void FelidaeAssembler::jumpIfFalse(IsaRegister control,Label target){fixups_.push_back({words_.size(),target,IsaOpcode::JumpIfFalse,control});emitABx(IsaOpcode::JumpIfFalse,control,0);}
void FelidaeAssembler::emitRegisters(std::span<const IsaRegister> values){for(std::size_t offset=0;offset<values.size();offset+=4){IsaWord word=0;for(std::size_t lane=0;lane<4&&offset+lane<values.size();++lane)word|=static_cast<IsaWord>(values[offset+lane])<<(lane*8u);emitExtension(word);}}
void FelidaeAssembler::call(IsaRegister d,std::uint16_t p,std::span<const IsaRegister> args){if(args.size()>255)throw IrError("assembler call has too many arguments");emitABC(IsaOpcode::Call,d,static_cast<std::uint8_t>(args.size()));emitExtension(p);emitRegisters(args);}
void FelidaeAssembler::callNamed(IsaRegister d,std::uint16_t p,std::span<const NamedRegister> args){if(args.size()>255)throw IrError("assembler named call has too many arguments");emitABC(IsaOpcode::CallNamed,d,static_cast<std::uint8_t>(args.size()));emitExtension(p);for(const auto& arg:args)emitExtension(static_cast<IsaWord>(arg.symbol)|(static_cast<IsaWord>(arg.value)<<16u));}
void FelidaeAssembler::callNative(IsaRegister d,std::uint16_t s){emitABx(IsaOpcode::CallNative,d,s);}
void FelidaeAssembler::returnValue(IsaRegister s){emitABC(IsaOpcode::Return,s);}
void FelidaeAssembler::makeFact(IsaRegister d,std::uint16_t s){emitABx(IsaOpcode::MakeFact,d,s);}
void FelidaeAssembler::getField(IsaRegister d,IsaRegister o,std::uint16_t s){emitABC(IsaOpcode::GetField,d,o);emitExtension(s);}
void FelidaeAssembler::setField(IsaRegister o,std::uint16_t s,IsaRegister v){emitABC(IsaOpcode::SetField,o,v);emitExtension(s);}
void FelidaeAssembler::queryFacts(IsaRegister d,std::uint16_t s,std::uint16_t p){emitABC(IsaOpcode::QueryFacts,d);emitExtension(static_cast<IsaWord>(s)|(static_cast<IsaWord>(p)<<16u));}
void FelidaeAssembler::makeArray(IsaRegister d,std::span<const IsaRegister> values){if(values.size()>255)throw IrError("assembler array has too many values");emitABC(IsaOpcode::MakeArray,d,static_cast<std::uint8_t>(values.size()));emitRegisters(values);}
void FelidaeAssembler::makeMap(IsaRegister d,std::span<const MapRegister> values){if(values.size()>255)throw IrError("assembler map has too many values");emitABC(IsaOpcode::MakeMap,d,static_cast<std::uint8_t>(values.size()));for(const auto& value:values)emitExtension(static_cast<IsaWord>(value.symbol)|(static_cast<IsaWord>(value.value)<<16u));}
void FelidaeAssembler::membership(IsaRegister d,IsaRegister value,IsaRegister peak,IsaRegister in,IsaRegister out){emitABC(IsaOpcode::Membership,d,value,peak);emitExtension(static_cast<IsaWord>(in)|(static_cast<IsaWord>(out)<<8u));}
void FelidaeAssembler::temporalRank(IsaRegister d,std::uint16_t effectiveAtField,std::uint16_t priorityField){emitABC(IsaOpcode::TemporalRank,d);emitExtension(static_cast<IsaWord>(effectiveAtField)|(static_cast<IsaWord>(priorityField)<<16u));}
void FelidaeAssembler::semanticEval(IsaRegister d,SemanticOperationId operation,std::span<const IsaRegister> inputs){const auto id=static_cast<std::uint16_t>(operation);if(inputs.size()>255||!isKnownSemanticOperation(id)||!semanticOperationAcceptsArity(id,inputs.size()))throw IrError("assembler semantic operation is invalid");emitABC(IsaOpcode::SemanticEval,d,static_cast<std::uint8_t>(inputs.size()));emitExtension(id);emitRegisters(inputs);}

void FelidaeAssembler::emitABC(IsaOpcode opcode, std::uint8_t a,
                               std::uint8_t b, std::uint8_t c) {
    if (!isKnownIsaOpcode(static_cast<std::uint8_t>(opcode))) throw IrError("assembler received an unknown ISA opcode");
    words_.push_back(encodeIsaABC(opcode, a, b, c));
}

void FelidaeAssembler::emitABx(IsaOpcode opcode, std::uint8_t a, std::uint16_t bx) {
    if (!isKnownIsaOpcode(static_cast<std::uint8_t>(opcode))) throw IrError("assembler received an unknown ISA opcode");
    words_.push_back(encodeIsaABx(opcode, a, bx));
}

void FelidaeAssembler::emitAx(IsaOpcode opcode, std::uint32_t ax) {
    if (!isKnownIsaOpcode(static_cast<std::uint8_t>(opcode)) || ax > 0x00ffffffu) {
        throw IrError("assembler received an invalid ISA Ax instruction");
    }
    words_.push_back(encodeIsaAx(opcode, ax));
}
void FelidaeAssembler::emitExtension(IsaWord value) { words_.push_back(value); }

std::vector<IsaWord> FelidaeAssembler::finish() && {
    for(const auto& fixup:fixups_){const auto found=labels_.find(fixup.label.id);if(found==labels_.end())throw IrError("assembler branch references an unbound label");if(fixup.opcode==IsaOpcode::Jump){if(found->second>0x00ffffffu)throw IrError("assembler jump target exceeds ISA range");words_[fixup.word]=encodeIsaAx(IsaOpcode::Jump,static_cast<std::uint32_t>(found->second));}else{if(found->second>=kIsaShortIndexLimit)throw IrError("assembler conditional target exceeds ISA range");words_[fixup.word]=encodeIsaABx(IsaOpcode::JumpIfFalse,fixup.control,static_cast<std::uint16_t>(found->second));}}
    return std::move(words_);
}

} // namespace Felidae
