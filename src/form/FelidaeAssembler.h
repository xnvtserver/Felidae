#pragma once

#include "FelidaeIsa.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace Felidae {

// Deterministic assembler for Felidae ISA words. Compiler code emits typed
// instruction fields through this API; learned components never receive an
// assembler and can only propose verified structured compiler IR.
class FelidaeAssembler {
public:
    struct Label { std::uint32_t id=0; bool operator==(const Label&)const=default; };
    struct NamedRegister { std::uint16_t symbol=0xffffu; IsaRegister value=0; };
    struct MapRegister { std::uint16_t symbol=0; IsaRegister value=0; };

    Label createLabel();
    void bind(Label label);

    void halt();
    void loadConstant(IsaRegister destination,std::uint16_t constant);
    void loadGlobal(IsaRegister destination,std::uint16_t symbol);
    void storeGlobal(std::uint16_t symbol,IsaRegister source);
    void move(IsaRegister destination,IsaRegister source);
    void binary(IsaOpcode opcode,IsaRegister destination,IsaRegister left,IsaRegister right);
    void booleanNot(IsaRegister destination,IsaRegister source);
    void jump(Label target);
    void jumpIfFalse(IsaRegister control,Label target);
    void call(IsaRegister destination,std::uint16_t procedure,std::span<const IsaRegister> arguments);
    void callNamed(IsaRegister destination,std::uint16_t procedure,std::span<const NamedRegister> arguments);
    void callNative(IsaRegister destination,std::uint16_t symbol);
    void returnValue(IsaRegister source);
    void makeFact(IsaRegister destination,std::uint16_t typeSymbol);
    void getField(IsaRegister destination,IsaRegister object,std::uint16_t fieldSymbol);
    void setField(IsaRegister object,std::uint16_t fieldSymbol,IsaRegister value);
    void queryFacts(IsaRegister destination,std::uint16_t typeSymbol,std::uint16_t procedure);
    void makeArray(IsaRegister destination,std::span<const IsaRegister> values);
    void makeMap(IsaRegister destination,std::span<const MapRegister> values);
    void membership(IsaRegister destination,IsaRegister value,IsaRegister peak,
                    IsaRegister fadesIn,IsaRegister fadesOut);
    void temporalRank(IsaRegister destination,std::uint16_t effectiveAtField,
                      std::uint16_t priorityField);
    void semanticEval(IsaRegister destination,SemanticOperationId operation,
                      std::span<const IsaRegister> inputs);

    std::size_t wordOffset() const noexcept { return words_.size(); }
    std::vector<IsaWord> finish() &&;

private:
    struct Fixup{std::size_t word=0;Label label;IsaOpcode opcode=IsaOpcode::Jump;IsaRegister control=0;};
    void emitABC(IsaOpcode opcode,std::uint8_t a=0,std::uint8_t b=0,std::uint8_t c=0);
    void emitABx(IsaOpcode opcode,std::uint8_t a,std::uint16_t bx);
    void emitAx(IsaOpcode opcode,std::uint32_t ax);
    void emitExtension(IsaWord value);
    void emitRegisters(std::span<const IsaRegister> values);
    std::vector<IsaWord> words_;
    std::uint32_t nextLabel_=1;
    std::unordered_map<std::uint32_t,std::size_t> labels_;
    std::vector<Fixup> fixups_;
};

} // namespace Felidae
