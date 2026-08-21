#include "MixfixStateModel.h"
#include "form/RuntimeStateModel.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <vector>

int main() {
    using namespace Felidae;

    class FixedModel final : public MixfixStateModel {
    public:
        std::vector<MixfixVocabularyId> transform(std::span<const SentencePieceId>,
                                                  const MixfixContext&) override {
            return {0, 1, 2, 3};
        }
    };
    class InvalidModel final : public MixfixStateModel {
    public:
        std::vector<MixfixVocabularyId> transform(std::span<const SentencePieceId>,
                                                  const MixfixContext&) override {
            return {4, 1, 2, 3};
        }
    };
    class OversizedModel final : public MixfixStateModel {
    public:
        std::vector<MixfixVocabularyId> transform(std::span<const SentencePieceId>,
                                                  const MixfixContext&) override {
            return {0, 1, 2, 3};
        }
    };
    class UnknownVocabularyModel final : public MixfixStateModel {
    public:
        std::vector<MixfixVocabularyId> transform(std::span<const SentencePieceId>,
                                                  const MixfixContext&) override {
            return {999};
        }
    };

    MixfixContext context;
    context.constantReferences = {0};
    context.outputVocabulary = {
        {MixfixIrTokenKind::Opcode, static_cast<IrWord>(IrOpcode::LoadConst)},
        {MixfixIrTokenKind::Register, 0},
        {MixfixIrTokenKind::ConstantReference, 0},
        {MixfixIrTokenKind::End, 0},
    };
    const std::vector<MixfixIrToken> valid = {
        {MixfixIrTokenKind::Opcode, static_cast<IrWord>(IrOpcode::LoadConst)},
        {MixfixIrTokenKind::Register, 0},
        {MixfixIrTokenKind::ConstantReference, 0},
        {MixfixIrTokenKind::End, 0},
    };
    const auto words = resolveMixfixIrTokens(valid, context);
    assert((words == std::vector<IrWord>{static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
                                         static_cast<IrWord>(IrOpcode::End)}));

    FelidaeIr ir;
    ir.words = words;
    ir.constants = {encodeIrNumber(1.0)};
    ir.constantKinds = {IrConstantKind::Number};
    ir.registerCount = 1;
    IrVerifier::verify(ir);

    FixedModel model;
    const std::vector<SentencePieceId> input = {1};
    const auto verified = compileVerifiedMixfixIr(model, input, context, ir);
    assert(verified.words == words);

    InvalidModel invalidModel;
    bool verifierRejected = false;
    try {
        (void)compileVerifiedMixfixIr(invalidModel, input, context, ir);
    } catch (const IrError&) {
        verifierRejected = true;
    }
    assert(verifierRejected);

    MixfixContext tightContext = context;
    tightContext.maximumOutputWords = 1;
    OversizedModel oversizedModel;
    bool oversizedRejected = false;
    try {
        (void)compileVerifiedMixfixIr(oversizedModel, input, tightContext, ir);
    } catch (const IrError&) {
        oversizedRejected = true;
    }
    assert(oversizedRejected);
    UnknownVocabularyModel unknownVocabularyModel;
    bool vocabularyRejected = false;
    try {
        (void)compileVerifiedMixfixIr(unknownVocabularyModel, input, context, ir);
    } catch (const IrError&) {
        vocabularyRejected = true;
    }
    assert(vocabularyRejected);

#ifdef FELIDAE_HAS_TORCH
    // A one-entry finite vocabulary makes autoregressive GRU decoding
    // deterministic while still exercising the actual LibTorch encoder,
    // recurrent state transfer, decoder, and projection path.
    GruMixfixStateModel::Configuration gruConfiguration;
    gruConfiguration.inputVocabularySize = 8;
    gruConfiguration.outputVocabularySize = 1;
    gruConfiguration.embeddingSize = 4;
    gruConfiguration.hiddenSize = 4;
    gruConfiguration.layerCount = 1;
    gruConfiguration.beginToken = 0;
    gruConfiguration.maximumDecodeSteps = 2;
    gruConfiguration.allowRandomInitialization = true;
    GruMixfixStateModel gruModel(gruConfiguration, {});
    MixfixContext gruContext;
    gruContext.maximumOutputWords = 2;
    gruContext.outputVocabulary = {{MixfixIrTokenKind::End, 0}};
    const auto gruTokens = gruModel.transform(input, gruContext);
    assert((gruTokens == std::vector<MixfixVocabularyId>{0}));

    // The VM recurrent backend has one shared finite output vocabulary for
    // both model loading and C++ training. It retains bounded references to
    // every supported input position rather than silently limiting models to
    // argument zero.
    const auto runtimeVocabulary = defaultRuntimeOutputVocabulary();
    assert(runtimeVocabulary.size() == 3 + 2 * kRuntimeModelReferenceLimit + 5);
    assert(runtimeVocabulary[3].kind == RuntimeOutputTokenKind::InputReference && runtimeVocabulary[3].value == 0);
    assert(runtimeVocabulary[3 + kRuntimeModelReferenceLimit - 1].kind == RuntimeOutputTokenKind::InputReference &&
           runtimeVocabulary[3 + kRuntimeModelReferenceLimit - 1].value == kRuntimeModelReferenceLimit - 1);
    assert(runtimeVocabulary[3 + kRuntimeModelReferenceLimit].kind == RuntimeOutputTokenKind::FactFromInput &&
           runtimeVocabulary[3 + kRuntimeModelReferenceLimit].value == 0);

    // The VM recurrent backend has its own finite output vocabulary.  A
    // reference token returns the original typed Value, so state-model use
    // never invents boolean truthiness or loses fact/text/container payloads.
    GruRuntimeStateModel::Configuration runtimeGruConfiguration;
    runtimeGruConfiguration.inputVocabularySize = 17;
    runtimeGruConfiguration.outputVocabularySize = 1;
    runtimeGruConfiguration.embeddingSize = 4;
    runtimeGruConfiguration.hiddenSize = 4;
    runtimeGruConfiguration.layerCount = 1;
    runtimeGruConfiguration.allowRandomInitialization = true;
    GruRuntimeStateModel runtimeGru(runtimeGruConfiguration,
                                    {{RuntimeOutputTokenKind::InputReference, 0}});
    RuntimeContext runtimeGruContext;
    runtimeGruContext.executionState = runtimeGru.createExecutionState();
    const VmValue runtimeText = VmText{{17, 23, 41}};
    assert(std::get<VmText>(runtimeGru.evaluate(RuntimeOperation{1}, {&runtimeText, 1},
                                                runtimeGruContext)).pieces ==
           std::vector<std::uint32_t>({17, 23, 41}));
    auto runtimeFact = std::make_shared<VmFact>();
    runtimeFact->type = 42;
    const VmValue runtimeFactValue = runtimeFact;
    assert(std::get<VmFactPtr>(runtimeGru.evaluate(RuntimeOperation{2}, {&runtimeFactValue, 1},
                                                    runtimeGruContext))->type == 42);
    GruRuntimeStateModel softRuntimeGru(runtimeGruConfiguration,
                                        {{RuntimeOutputTokenKind::DegreeMilli, 725}});
    RuntimeContext softRuntimeContext;
    softRuntimeContext.executionState = softRuntimeGru.createExecutionState();
    const auto softResult = softRuntimeGru.evaluate(RuntimeOperation{3}, {}, softRuntimeContext);
    assert(std::get<VmDegree>(softResult).value == 0.725);
#endif

    FelidaeIr arithmetic;
    arithmetic.registerCount = 3;
    arithmetic.constants = {encodeIrNumber(6.0), encodeIrNumber(7.0)};
    arithmetic.words = {
        static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
        static_cast<IrWord>(IrOpcode::LoadConst), 1, 1,
        static_cast<IrWord>(IrOpcode::Mul), 2, 0, 1,
        static_cast<IrWord>(IrOpcode::Return), 2, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    class NoRuntime final : public VmRuntime {
    public:
        VmValue callNativeSymbol(IrSymbolRef symbol) override { return static_cast<double>(symbol); }
    } noRuntime;
    RegisterVm nativeVm;
    const auto arithmeticResult = nativeVm.execute(arithmetic, noRuntime, VmNil{});
    assert(std::get<double>(arithmeticResult) == 42.0);
    // The branch protocol is deliberately narrower than host-language
    // truthiness. Every non-boolean VM type remains usable data and must not
    // become an implicit control decision.
    assert(noRuntime.shouldBranchFalse(VmNil{}));
    assert(noRuntime.shouldBranchFalse(false));
    assert(!noRuntime.shouldBranchFalse(true));
    assert(!noRuntime.shouldBranchFalse(0.0));
    assert(!noRuntime.shouldBranchFalse(VmDegree(0.0)));
    assert(!noRuntime.shouldBranchFalse(VmText{{1, 2}}));
    assert(!noRuntime.shouldBranchFalse(std::make_shared<VmArray>()));
    assert(!noRuntime.shouldBranchFalse(std::make_shared<VmMap>()));
    assert(!noRuntime.shouldBranchFalse(std::make_shared<VmFact>()));

    FelidaeIr factProgram;
    factProgram.registerCount = 2;
    factProgram.symbols = {50, 51, 12};
    factProgram.constants = {encodeIrNumber(12.0)};
    factProgram.words = {
        static_cast<IrWord>(IrOpcode::MakeFact), 0, 0,
        static_cast<IrWord>(IrOpcode::LoadConst), 1, 0,
        static_cast<IrWord>(IrOpcode::SetField), 0, 1, 1,
        static_cast<IrWord>(IrOpcode::Return), 0, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    const auto factValue = nativeVm.execute(factProgram, noRuntime, VmNil{});
    const auto fact = std::get<VmFactPtr>(factValue);
    assert(fact->type == 50 && fact->fields.size() == 1);
    assert(fact->fields.front().first == 51);
    assert(std::get<double>(fact->fields.front().second) == 12.0);

    FelidaeIr nativeCall;
    nativeCall.registerCount = 1;
    nativeCall.symbols = {12};
    nativeCall.words = {
        static_cast<IrWord>(IrOpcode::CallNative), 0, 0,
        static_cast<IrWord>(IrOpcode::Return), 0, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    assert(std::get<double>(nativeVm.execute(nativeCall, noRuntime, VmNil{})) == 12.0);

    class SymbolRuntime final : public VmRuntime {
    public:
        VmValue loadSymbol(IrSymbolRef symbol) override { return values.at(symbol); }
        void storeSymbol(IrSymbolRef symbol, const VmValue& value) override { values[symbol] = value; }
        std::unordered_map<IrSymbolRef, VmValue> values;
    } symbolRuntime;
    FelidaeIr symbolStore;
    symbolStore.registerCount = 1;
    symbolStore.symbols = {7};
    symbolStore.constants = {encodeIrNumber(9.0)};
    symbolStore.words = {
        static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
        static_cast<IrWord>(IrOpcode::StoreSymbol), 0, 0,
        static_cast<IrWord>(IrOpcode::LoadSymbol), 0, 0,
        static_cast<IrWord>(IrOpcode::Return), 0, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    assert(std::get<double>(nativeVm.execute(symbolStore, symbolRuntime, VmNil{})) == 9.0);

    FelidaeIr directProcedure;
    directProcedure.registerCount = 1;
    directProcedure.constants = {encodeIrNumber(7.0)};
    directProcedure.words = {
        static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
        static_cast<IrWord>(IrOpcode::Return), 0, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    FelidaeIr directCaller;
    directCaller.registerCount = 1;
    directCaller.symbols = {99};
    directCaller.words = {
        static_cast<IrWord>(IrOpcode::Call), 0, 0, 0,
        static_cast<IrWord>(IrOpcode::Return), 0, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    FelidaeKnowledgeRuntime procedureRuntime({{99, IrProcedure{directProcedure, {}, {}, {}}}});
    assert(std::get<double>(nativeVm.execute(directCaller, procedureRuntime, VmNil{})) == 7.0);
    FelidaeIr recursiveProcedure;
    recursiveProcedure.registerCount = 1;
    recursiveProcedure.symbols = {77};
    recursiveProcedure.words = {
        static_cast<IrWord>(IrOpcode::Call), 0, 0, 0,
        static_cast<IrWord>(IrOpcode::Return), 0, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    FelidaeIr recursiveCaller = directCaller;
    recursiveCaller.symbols = {77};
    FelidaeKnowledgeRuntime boundedRecursionRuntime({{77, IrProcedure{recursiveProcedure, {}, {}, {}}}},
                                             nullptr, 1024, 2);
    bool recursionRejected = false;
    try {
        (void)nativeVm.execute(recursiveCaller, boundedRecursionRuntime, VmNil{});
    } catch (const IrError&) {
        recursionRejected = true;
    }
    assert(recursionRejected);

    class StatefulSemanticModel final : public RuntimeStateModel {
    public:
        std::shared_ptr<void> createExecutionState() override {
            return std::make_shared<std::size_t>(0);
        }
        VmValue evaluate(const RuntimeOperation& operation, std::span<const VmValue> inputs,
                         RuntimeContext& context) override {
            assert(operation.symbol == 99 && inputs.size() == 1);
            auto state = std::static_pointer_cast<std::size_t>(context.executionState);
            // Generic test runtimes may not opt into the production lifecycle;
            // they still receive an execution-local context and never persist
            // this fallback state beyond this RegisterVm invocation.
            if (!state) {
                state = std::make_shared<std::size_t>(0);
                context.executionState = state;
            }
            ++*state;
            return std::get<double>(inputs.front()) + static_cast<double>(*state);
        }
    } semanticModel;
    class SemanticRuntime final : public VmRuntime {
    public:
        explicit SemanticRuntime(RuntimeStateModel& model) : model_(model) {}
        RuntimeStateModel* runtimeStateModel() override { return &model_; }
        RuntimeContext makeRuntimeContext(const FelidaeIr&, const VmValue&) const override {
            RuntimeContext context;
            context.maximumSemanticSteps = 2;
            return context;
        }
    private:
        RuntimeStateModel& model_;
    } semanticRuntime(semanticModel);
    FelidaeIr semanticProgram;
    semanticProgram.registerCount = 2;
    semanticProgram.symbols = {99};
    semanticProgram.constants = {encodeIrNumber(4.0)};
    semanticProgram.words = {
        static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
        static_cast<IrWord>(IrOpcode::SemanticEval), 1, 0, 1, 0,
        static_cast<IrWord>(IrOpcode::Return), 1, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    assert(std::get<double>(nativeVm.execute(semanticProgram, semanticRuntime, VmNil{})) == 5.0);
    FelidaeIr ssmProgram = semanticProgram;
    ssmProgram.words[3] = static_cast<IrWord>(IrOpcode::SsmProcess);
    IrVerifier::verify(ssmProgram);
    assert(std::get<double>(nativeVm.execute(ssmProgram, semanticRuntime, VmNil{})) == 5.0);
    // A new VM execution owns a new RuntimeContext; recurrent state cannot
    // leak across requests even when the backend instance is reused.
    assert(std::get<double>(nativeVm.execute(semanticProgram, semanticRuntime, VmNil{})) == 5.0);
    FelidaeKnowledgeRuntime directSemanticRuntime({}, &semanticModel);
    assert(std::get<double>(nativeVm.execute(semanticProgram, directSemanticRuntime, VmNil{})) == 5.0);
    // Facts asserted earlier in the same VM program are refreshed into the
    // learned-operation context; a model never trains on a snapshot it cannot
    // observe during production execution.
    class KnowledgeCheckingModel final : public RuntimeStateModel {
    public:
        VmValue evaluate(const RuntimeOperation& operation, std::span<const VmValue> inputs,
                         RuntimeContext& context) override {
            assert(operation.symbol == 99 && inputs.size() == 1);
            assert(context.knowledge.factTypes.size() == 1 && context.knowledge.factTypes[0] == 77);
            assert(context.knowledge.factTypeCounts.size() == 1 &&
                   context.knowledge.factTypeCounts[0].first == 77 &&
                   context.knowledge.factTypeCounts[0].second == 1);
            assert(std::holds_alternative<VmFactPtr>(inputs.front()));
            return inputs.front();
        }
    } knowledgeModel;
    FelidaeIr knowledgeProgram;
    knowledgeProgram.registerCount = 2;
    knowledgeProgram.symbols = {77, 99};
    knowledgeProgram.words = {
        static_cast<IrWord>(IrOpcode::MakeFact), 0, 0,
        static_cast<IrWord>(IrOpcode::SsmProcess), 1, 1, 1, 0,
        static_cast<IrWord>(IrOpcode::Return), 1, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    FelidaeKnowledgeRuntime knowledgeRuntime({}, &knowledgeModel);
    assert(std::holds_alternative<VmFactPtr>(nativeVm.execute(knowledgeProgram, knowledgeRuntime, VmNil{})));
    // Nested calls are part of one top-level execution: the SSM state and
    // semantic budget are shared across their fresh register frames.
    FelidaeIr semanticCaller;
    semanticCaller.registerCount = 2;
    semanticCaller.symbols = {99};
    semanticCaller.words = {
        static_cast<IrWord>(IrOpcode::Call), 0, 0, 0,
        static_cast<IrWord>(IrOpcode::Call), 1, 0, 0,
        static_cast<IrWord>(IrOpcode::Return), 1, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    FelidaeKnowledgeRuntime nestedSemanticRuntime({{99, IrProcedure{semanticProgram, {}, {}, {}}}},
                                          &semanticModel, 2);
    assert(std::get<double>(nativeVm.execute(semanticCaller, nestedSemanticRuntime, VmNil{})) == 6.0);
    FelidaeIr overLimitSemanticProgram;
    overLimitSemanticProgram.registerCount = 4;
    overLimitSemanticProgram.symbols = {99};
    overLimitSemanticProgram.constants = {encodeIrNumber(1.0)};
    overLimitSemanticProgram.words = {
        static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
        static_cast<IrWord>(IrOpcode::SemanticEval), 1, 0, 1, 0,
        static_cast<IrWord>(IrOpcode::SemanticEval), 2, 0, 1, 1,
        static_cast<IrWord>(IrOpcode::SemanticEval), 3, 0, 1, 2,
        static_cast<IrWord>(IrOpcode::Return), 3, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    bool semanticLimitRejected = false;
    try {
        (void)nativeVm.execute(overLimitSemanticProgram, semanticRuntime, VmNil{});
    } catch (const IrError&) {
        semanticLimitRejected = true;
    }
    assert(semanticLimitRejected);
    FelidaeKnowledgeRuntime oneStepSemanticRuntime({}, &semanticModel, 1);
    bool directSemanticLimitRejected = false;
    try {
        (void)nativeVm.execute(overLimitSemanticProgram, oneStepSemanticRuntime, VmNil{});
    } catch (const IrError&) {
        directSemanticLimitRejected = true;
    }
    assert(directSemanticLimitRejected);
    bool unavailableSemanticRejected = false;
    try {
        (void)nativeVm.execute(semanticProgram, noRuntime, VmNil{});
    } catch (const IrError&) {
        unavailableSemanticRejected = true;
    }
    assert(unavailableSemanticRejected);
    class InvalidSemanticModel final : public RuntimeStateModel {
    public:
        VmValue evaluate(const RuntimeOperation&, std::span<const VmValue>, RuntimeContext&) override {
            return VmMapPtr{};
        }
    } invalidSemanticModel;
    SemanticRuntime invalidSemanticRuntime(invalidSemanticModel);
    bool invalidSemanticRejected = false;
    try {
        (void)nativeVm.execute(semanticProgram, invalidSemanticRuntime, VmNil{});
    } catch (const IrError&) {
        invalidSemanticRejected = true;
    }
    assert(invalidSemanticRejected);

    FelidaeIr branchSkipsInitialization;
    branchSkipsInitialization.registerCount = 2;
    branchSkipsInitialization.constants = {1};
    branchSkipsInitialization.constantKinds = {IrConstantKind::Boolean};
    branchSkipsInitialization.words = {
        static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
        static_cast<IrWord>(IrOpcode::JumpIfFalse), 0, 9,
        static_cast<IrWord>(IrOpcode::LoadConst), 1, 0,
        static_cast<IrWord>(IrOpcode::Return), 1, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    bool flowRejected = false;
    try {
        IrVerifier::verify(branchSkipsInitialization);
    } catch (const IrError&) {
        flowRejected = true;
    }
    assert(flowRejected);

    FelidaeIr overflowingCall;
    overflowingCall.registerCount = 1;
    overflowingCall.symbols = {0};
    overflowingCall.words = {static_cast<IrWord>(IrOpcode::Call), 0, 0,
                             std::numeric_limits<IrWord>::max()};
    bool overflowRejected = false;
    try {
        IrVerifier::verify(overflowingCall);
    } catch (const IrError&) {
        overflowRejected = true;
    }
    assert(overflowRejected);

    FelidaeIr invalidSemanticOperation;
    invalidSemanticOperation.registerCount = 1;
    invalidSemanticOperation.words = {
        static_cast<IrWord>(IrOpcode::SemanticEval), 0, 0, 0,
        static_cast<IrWord>(IrOpcode::Return), 0, 0,
        static_cast<IrWord>(IrOpcode::End),
    };
    bool invalidSemanticOperationRejected = false;
    try {
        IrVerifier::verify(invalidSemanticOperation);
    } catch (const IrError&) {
        invalidSemanticOperationRejected = true;
    }
    assert(invalidSemanticOperationRejected);

    FelidaeIr overflowingSemanticOperation;
    overflowingSemanticOperation.registerCount = 1;
    overflowingSemanticOperation.symbols = {1};
    overflowingSemanticOperation.words = {
        static_cast<IrWord>(IrOpcode::SemanticEval), 0, 0,
        std::numeric_limits<IrWord>::max(),
    };
    bool overflowingSemanticRejected = false;
    try {
        IrVerifier::verify(overflowingSemanticOperation);
    } catch (const IrError&) {
        overflowingSemanticRejected = true;
    }
    assert(overflowingSemanticRejected);

    FelidaeIr badSourceMap = arithmetic;
    badSourceMap.sourceMap = {IrSourceMapEntry{1, {1, 1, 1, 2}}};
    bool sourceMapRejected = false;
    try {
        IrVerifier::verify(badSourceMap);
    } catch (const IrError&) {
        sourceMapRejected = true;
    }
    assert(sourceMapRejected);

    bool rejected = false;
    try {
        const std::vector<MixfixIrToken> noEnd = {
            {MixfixIrTokenKind::Opcode, kIrOpcodeCount}};
        (void)resolveMixfixIrTokens(noEnd, context);
    } catch (const IrError&) {
        rejected = true;
    }
    assert(rejected);
}
