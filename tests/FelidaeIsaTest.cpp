#include "form/FelidaeAssembler.h"
#include "form/BinaryIsa.h"
#include "form/FelidaeIsa.h"
#include "form/IsaLowerer.h"

#include <array>
#include <cassert>
#include <functional>
#include <filesystem>
#include <unordered_map>

namespace {

bool rejects(const std::function<void()>& action) {
    try { action(); }
    catch (const Felidae::IrError&) { return true; }
    return false;
}

} // namespace

int main() {
    using namespace Felidae;

    static_assert(sizeof(IsaWord) == 4);
    static_assert(static_cast<std::uint8_t>(IsaOpcode::LoadConstant) == 0x10);
    static_assert(static_cast<std::uint8_t>(IsaOpcode::SemanticEval) == 0x80);
    static_assert(kFelidaeIsaVersion == 1);

    const auto encoded = encodeIsaABC(IsaOpcode::Add, 3, 4, 5);
    const auto decoded = decodeIsaWord(encoded);
    assert(decoded.opcode == IsaOpcode::Add && decoded.a == 3 && decoded.b == 4 && decoded.c == 5);

    FelidaeAssembler assembler;
    assembler.loadConstant(0,0);
    assembler.returnValue(0);
    assembler.halt();
    IsaBlock valid{std::move(assembler).finish(), 1};
    IsaVerifier::verify(valid, {1, 0, 0});

    FelidaeAssembler labelled;
    const auto exit=labelled.createLabel();
    labelled.jump(exit);
    labelled.loadConstant(0,0);
    labelled.bind(exit);
    labelled.halt();
    const IsaBlock labelledBlock{std::move(labelled).finish(),1};
    assert(decodeIsaWord(labelledBlock.words.front()).ax==2);
    IsaVerifier::verify(labelledBlock,{1,0,0});

    assert(rejects([]{FelidaeAssembler broken;const auto missing=broken.createLabel();broken.jump(missing);(void)std::move(broken).finish();}));

    FelidaeAssembler semantic;
    semantic.loadConstant(0,0);
    const std::array<IsaRegister,1> semanticInputs{0};
    semantic.semanticEval(0,SemanticOperationId::Identity,semanticInputs);
    semantic.returnValue(0);
    const IsaBlock semanticBlock{std::move(semantic).finish(),1};
    IsaVerifier::verify(semanticBlock,{1,0,0});

    assert(rejects([] {
        FelidaeAssembler invalid;
        invalid.semanticEval(0, SemanticOperationId::Identity, {});
    }));
    IsaBlock invalidSemanticArity{{
        encodeIsaABC(IsaOpcode::SemanticEval, 0, 0),
        static_cast<IsaWord>(SemanticOperationId::Identity),
        encodeIsaABC(IsaOpcode::Return, 0)}, 1};
    assert(rejects([&] {
        IsaVerifier::verify(invalidSemanticArity, {0, 0, 0});
    }));

    auto invalidOpcode = valid;
    invalidOpcode.words[0] = 0xfeu;
    assert(rejects([&] { IsaVerifier::verify(invalidOpcode, {1, 0, 0}); }));

    auto invalidRegister = valid;
    invalidRegister.words[0] = encodeIsaABx(IsaOpcode::LoadConstant, 1, 0);
    assert(rejects([&] { IsaVerifier::verify(invalidRegister, {1, 0, 0}); }));

    auto invalidConstant = valid;
    invalidConstant.words[0] = encodeIsaABx(IsaOpcode::LoadConstant, 0, 1);
    assert(rejects([&] { IsaVerifier::verify(invalidConstant, {1, 0, 0}); }));

    auto nonCanonicalReturn = valid;
    nonCanonicalReturn.words[1] |= 0x00010000u;
    assert(rejects([&] {
        IsaVerifier::verify(nonCanonicalReturn, {1, 0, 0});
    }));

    IsaBlock nonCanonicalPackedCall{{
        encodeIsaABx(IsaOpcode::LoadConstant, 0, 0),
        encodeIsaABC(IsaOpcode::Call, 0, 1),
        0,
        0x00000100u,
        encodeIsaABC(IsaOpcode::Return, 0)}, 1};
    assert(rejects([&] {
        IsaVerifier::verify(nonCanonicalPackedCall, {1, 0, 1});
    }));

    IsaBlock invalidProcedure{{encodeIsaABC(IsaOpcode::Call, 0, 0), 1,
                               encodeIsaABC(IsaOpcode::Return, 0)}, 1};
    assert(rejects([&] { IsaVerifier::verify(invalidProcedure, {0, 0, 1}); }));

    IsaBlock invalidBranch{{encodeIsaAx(IsaOpcode::Jump, 9),
                            encodeIsaABC(IsaOpcode::Halt)}, 1};
    assert(rejects([&] { IsaVerifier::verify(invalidBranch, {}); }));

    IsaBlock uninitialized{{encodeIsaABC(IsaOpcode::Add,0,0,0),
                            encodeIsaABC(IsaOpcode::Return,0)},1};
    assert(rejects([&]{IsaVerifier::verify(uninitialized,{});}));

    IsaBlock incompleteCall{{encodeIsaABC(IsaOpcode::Call, 0, 1), 0}, 1};
    assert(rejects([&] { IsaVerifier::verify(incompleteCall, {0, 0, 1}); }));

    IsaBlock invalidSemantic{{encodeIsaABC(IsaOpcode::SemanticEval, 0, 0), 0xffff,
                              encodeIsaABC(IsaOpcode::Return, 0)}, 1};
    assert(rejects([&] { IsaVerifier::verify(invalidSemantic, {}); }));

    FelidaeIr ir;
    ir.registerCount = 1;
    ir.constants = {encodeIrNumber(42.0)};
    ir.constantKinds = {IrConstantKind::Number};
    ir.words = {static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
                static_cast<IrWord>(IrOpcode::Return), 0, 0,
                static_cast<IrWord>(IrOpcode::End)};
    const auto lowered = IsaLowerer::lower(ir, std::unordered_map<IrSymbolRef, std::uint16_t>{});
    IsaVerifier::verify(lowered, {ir.constants.size(), ir.symbols.size(), 0});
    assert(decodeIsaWord(lowered.words[0]).opcode == IsaOpcode::LoadConstant);
    assert(decodeIsaWord(lowered.words[1]).opcode == IsaOpcode::Return);

    const IrSymbolRef mainSymbol = 0x1234;
    IrModule irModule;
    irModule.entryProcedure = mainSymbol;
    irModule.ir.registerCount = 1;
    irModule.ir.symbols = {mainSymbol};
    irModule.ir.words = {static_cast<IrWord>(IrOpcode::Call), 0, 0, 0,
                         static_cast<IrWord>(IrOpcode::Return), 0, 0,
                         static_cast<IrWord>(IrOpcode::End)};
    IrProcedure procedure;
    procedure.ir = ir;
    irModule.procedures.emplace(mainSymbol, std::move(procedure));
    const auto isaModule = IsaLowerer::lowerModule(irModule);
    verifyIsaModule(isaModule);
    auto forgedDisplayModule = isaModule;
    forgedDisplayModule.symbolNames = {{mainSymbol, "misleading-name"}};
    assert(rejects([&] { verifyIsaModule(forgedDisplayModule); }));
    FelidaeKnowledgeRuntime runtime;
    RegisterVm vm;
    assert(std::get<double>(vm.executeIsaMain(isaModule, runtime)) == 42.0);

    const std::filesystem::path testOutputDirectory(FELIDAE_TEST_OUTPUT_DIR);
    std::filesystem::create_directories(testOutputDirectory);
    const auto binary=testOutputDirectory/"felidae_isa_v1_test.bin";
    writeBinaryIsa(binary,isaModule);
    const auto loaded=loadBinaryIsa(binary);
    assert(loaded.isaVersion==kFelidaeIsaVersion);
    FelidaeKnowledgeRuntime loadedRuntime;
    assert(std::get<double>(vm.executeIsaMain(loaded,loadedRuntime))==42.0);
    std::error_code ignored;std::filesystem::remove(binary,ignored);

    IrModule booleanModule;
    booleanModule.entryProcedure=mainSymbol;
    booleanModule.ir=irModule.ir;
    IrProcedure booleanProcedure;
    booleanProcedure.ir.registerCount=3;
    booleanProcedure.ir.constants={encodeIrNumber(1.0),encodeIrNumber(2.0)};
    booleanProcedure.ir.constantKinds={IrConstantKind::Number,IrConstantKind::Number};
    booleanProcedure.ir.words={static_cast<IrWord>(IrOpcode::LoadConst),0,0,
                               static_cast<IrWord>(IrOpcode::LoadConst),1,1,
                               static_cast<IrWord>(IrOpcode::Compare),2,0,1,static_cast<IrWord>(IrComparison::Less),
                               static_cast<IrWord>(IrOpcode::Return),2,0,
                               static_cast<IrWord>(IrOpcode::End)};
    booleanModule.procedures.emplace(mainSymbol,std::move(booleanProcedure));
    const auto booleanIsa=IsaLowerer::lowerModule(booleanModule);
    FelidaeKnowledgeRuntime booleanRuntime;
    const auto booleanResult=vm.executeIsaMain(booleanIsa,booleanRuntime);
    assert(std::holds_alternative<double>(booleanResult));
    assert(std::get<double>(booleanResult)==1.0);

    constexpr IrSymbolRef parentType=0x2001,childType=0x2002;
    IsaModule hierarchyModule=isaModule;
    hierarchyModule.factTypes={{parentType,{},{}},{childType,{parentType},{}}};
    FelidaeAssembler hierarchyAssembler;
    hierarchyAssembler.makeFact(0,0);
    hierarchyAssembler.makeFact(1,1);
    hierarchyAssembler.binary(IsaOpcode::HierarchyIsA,2,0,1);
    hierarchyAssembler.returnValue(2);
    hierarchyModule.procedures[0].program.code={std::move(hierarchyAssembler).finish(),3};
    hierarchyModule.procedures[0].program.constants.clear();
    hierarchyModule.procedures[0].program.constantKinds.clear();
    hierarchyModule.procedures[0].program.symbols={childType,parentType};
    hierarchyModule.procedures[0].program.texts.clear();
    hierarchyModule.procedures[0].program.sourceMap.clear();
    FelidaeKnowledgeRuntime hierarchyRuntime;
    assert(std::get<double>(vm.executeIsaMain(hierarchyModule,hierarchyRuntime))==1.0);

    constexpr IrSymbolRef eventType=0x3001,effectiveField=0x3002,priorityField=0x3003;
    IsaModule temporalModule=isaModule;
    temporalModule.factTypes={{eventType,{},{}}};
    FelidaeAssembler temporalAssembler;
    temporalAssembler.makeFact(0,0);
    temporalAssembler.loadConstant(1,0);
    temporalAssembler.setField(0,1,1);
    temporalAssembler.loadConstant(2,1);
    temporalAssembler.setField(0,2,2);
    temporalAssembler.temporalRank(3,1,2);
    temporalAssembler.returnValue(3);
    temporalModule.procedures[0].program.code={std::move(temporalAssembler).finish(),4};
    temporalModule.procedures[0].program.constants={encodeIrNumber(10.0),encodeIrNumber(2.0)};
    temporalModule.procedures[0].program.constantKinds={IrConstantKind::Number,IrConstantKind::Number};
    temporalModule.procedures[0].program.symbols={eventType,effectiveField,priorityField};
    temporalModule.procedures[0].program.texts.clear();
    temporalModule.procedures[0].program.sourceMap.clear();
    FelidaeKnowledgeRuntime temporalRuntime;
    const auto ranked=std::get<VmArrayPtr>(vm.executeIsaMain(temporalModule,temporalRuntime));
    assert(ranked&&ranked->values.size()==1&&std::holds_alternative<VmFactPtr>(ranked->values.front()));

    return 0;
}
