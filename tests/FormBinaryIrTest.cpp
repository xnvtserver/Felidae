#include "form/BinaryIr.h"
#include "form/RuntimeTraining.h"
#include "Symbol.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

int main() {
    using namespace Felidae;

    const auto mainSymbol = symbolIdForName("main");
    IrModule module;
    module.entryProcedure = mainSymbol;
    module.ir.registerCount = 1;
    module.ir.symbols = {mainSymbol};
    module.ir.words = {
        static_cast<IrWord>(IrOpcode::Call), 0, 0, 0,
        static_cast<IrWord>(IrOpcode::Return), 0, 0,
        static_cast<IrWord>(IrOpcode::End)};

    IrProcedure main;
    // Register 22 happens to be the numeric value of the legacy opcode.
    // It remains a legal operand; only instruction-boundary opcodes are
    // forbidden from a binary module.
    main.ir.registerCount = 23;
    main.ir.constants = {encodeIrNumber(42.0)};
    main.ir.constantKinds = {IrConstantKind::Number};
    main.ir.words = {
        static_cast<IrWord>(IrOpcode::LoadConst), 22, 0,
        static_cast<IrWord>(IrOpcode::Return), 22, 0,
        static_cast<IrWord>(IrOpcode::End)};
    module.procedures.emplace(mainSymbol, std::move(main));

    const auto path = std::filesystem::temp_directory_path() / "felidae_form_standalone.bin";
    writeBinaryIr(path, module);
    std::array<char, 8> magic{};
    { std::ifstream input(path, std::ios::binary); input.read(magic.data(), magic.size()); }
    assert((magic == std::array<char, 8>{'F','E','L','B','I','N','\0','\0'}));
    const auto loaded = loadBinaryIr(path);
    FelidaeKnowledgeRuntime runtime(loaded.procedures);
    RegisterVm vm;
    assert(std::get<double>(vm.executeMain(loaded, runtime)) == 42.0);

    // FELIR/.fir is deliberately incompatible with the FELBIN v8 container,
    // even when a legacy artifact is merely renamed to the new extension.
    const auto legacyPath = std::filesystem::temp_directory_path() / "felidae_legacy_renamed.bin";
    std::filesystem::copy_file(path, legacyPath, std::filesystem::copy_options::overwrite_existing);
    { std::fstream legacy(legacyPath, std::ios::binary | std::ios::in | std::ios::out);
      const std::array<char, 8> legacyMagic{'F','E','L','I','R','\0','\0','\0'};
      const std::array<char, 4> legacyVersion{7, 0, 0, 0};
      legacy.write(legacyMagic.data(), legacyMagic.size()); legacy.write(legacyVersion.data(), legacyVersion.size()); legacy.close(); }
    bool legacyRejected = false;
    try { (void)loadBinaryIr(legacyPath); }
    catch (const IrError& error) { legacyRejected = std::string(error.what()).find("legacy .fir") != std::string::npos; }
    assert(legacyRejected);
    std::error_code ignored;
    std::filesystem::remove(legacyPath, ignored);

    // A FELBIN container with the former version is also rejected. This is
    // separate from the legacy magic check so format evolution is explicit.
    const auto oldVersionPath = std::filesystem::temp_directory_path() / "felidae_v7.bin";
    std::filesystem::copy_file(path, oldVersionPath, std::filesystem::copy_options::overwrite_existing);
    { std::fstream oldVersion(oldVersionPath, std::ios::binary | std::ios::in | std::ios::out);
      oldVersion.seekp(8); const std::array<char, 4> version{7, 0, 0, 0}; oldVersion.write(version.data(), version.size()); oldVersion.close(); }
    bool oldVersionRejected = false;
    try { (void)loadBinaryIr(oldVersionPath); }
    catch (const IrError& error) { oldVersionRejected = std::string(error.what()).find("version") != std::string::npos; }
    assert(oldVersionRejected);
    std::filesystem::remove(oldVersionPath, ignored);

    const auto truncatedPath = std::filesystem::temp_directory_path() / "felidae_truncated.bin";
    { std::ofstream truncated(truncatedPath, std::ios::binary); truncated.write("FELBIN", 6); }
    bool truncatedRejected = false;
    try { (void)loadBinaryIr(truncatedPath); }
    catch (const IrError& error) { truncatedRejected = std::string(error.what()).find("truncated") != std::string::npos; }
    assert(truncatedRejected);
    std::filesystem::remove(truncatedPath, ignored);

    // Module verification closes the gap between a structurally valid call
    // operand and an actual procedure that can be invoked after loading.
    auto unknownCall = module;
    unknownCall.ir.symbols = {symbolIdForName("not_a_procedure")};
    bool unknownCallRejected = false;
    try { writeBinaryIr(path, unknownCall); }
    catch (const IrError&) { unknownCallRejected = true; }
    assert(unknownCallRejected);
    // A failed write must leave the previously verified artifact intact.
    assert(std::get<double>(vm.executeMain(loadBinaryIr(path), runtime)) == 42.0);

    // Positional binding identity and named-call spelling identity are
    // deliberately separate metadata fields. Named calls must use the latter.
    const auto positionalName = symbolIdForName("internal_value");
    const auto publicName = symbolIdForName("value");
    IrProcedure namedProcedure;
    namedProcedure.positionalParameters = {positionalName};
    namedProcedure.namedParameters = {publicName};
    namedProcedure.ir.registerCount = 1;
    namedProcedure.ir.symbols = {positionalName};
    namedProcedure.ir.words = {
        static_cast<IrWord>(IrOpcode::LoadSymbol), 0, 0,
        static_cast<IrWord>(IrOpcode::Return), 0, 0,
        static_cast<IrWord>(IrOpcode::End)};
    FelidaeKnowledgeRuntime namedRuntime({{mainSymbol, namedProcedure}});
    const VmCallArgument namedArgument{publicName, 99.0};
    assert(std::get<double>(namedRuntime.callSymbolNamed(
        mainSymbol, std::span<const VmCallArgument>{&namedArgument, 1})) == 99.0);

    // Bindings are immutable in every frame, including the module-global
    // initializer frame. Crafted IR therefore cannot introduce mutable
    // globals that source compilation would have rejected.
    FelidaeKnowledgeRuntime immutableRuntime({{mainSymbol, namedProcedure}});
    const auto immutableSymbol = symbolIdForName("immutable");
    immutableRuntime.storeSymbol(immutableSymbol, 1.0);
    bool globalRebindRejected = false;
    try { immutableRuntime.storeSymbol(immutableSymbol, 2.0); }
    catch (const IrError&) { globalRebindRejected = true; }
    assert(globalRebindRejected);

    // Fact analysis stays numeric/structural: hierarchy queries return type
    // IDs, ranking returns ordered facts, and Gaussian membership is a degree
    // rather than a boolean predicate.
    VmFactStore store;
    const auto animal = symbolIdForName("Animal");
    const auto mammal = symbolIdForName("Mammal");
    const auto feline = symbolIdForName("Feline");
    const auto canine = symbolIdForName("Canine");
    store.registerType(animal, {});
    store.registerType(mammal, {animal});
    store.registerType(feline, {mammal});
    store.registerType(canine, {mammal});
    assert(store.leastCommonAncestors(feline, canine) == std::vector<IrSymbolRef>{mammal});
    assert(store.mostGeneralCommonAncestors(feline, canine) == std::vector<IrSymbolRef>{animal});
    const auto observedAt = symbolIdForName("fx.observed_at");
    const auto priority = symbolIdForName("priority");
    auto older = std::make_shared<VmFact>();
    older->type = feline; older->fields = {{observedAt, 10.0}, {priority, 90.0}};
    auto newer = std::make_shared<VmFact>();
    newer->type = canine; newer->fields = {{observedAt, 20.0}, {priority, 10.0}};
    auto newerHighPriority = std::make_shared<VmFact>();
    newerHighPriority->type = canine; newerHighPriority->fields = {{observedAt, 20.0}, {priority, 20.0}};
    store.retain(older); store.retain(newer); store.retain(newerHighPriority);
    const auto ranked = store.rankByTimeAndPriority(observedAt, priority);
    assert(ranked.size() == 3 && ranked.front().fact == newerHighPriority && ranked[1].fact == newer);
    const VmGaussianProfile neutral{50.0, 30.0, 70.0};
    assert(std::abs(VmFactStore::gaussianMembership(50.0, neutral) - 1.0) < 1e-12);
    assert(std::abs(VmFactStore::gaussianMembership(30.0, neutral) - 0.01) < 1e-12);
    assert(std::abs(VmFactStore::gaussianMembership(70.0, neutral) - 0.01) < 1e-12);
    // The published overlapping profile scale has a typed degree at every
    // point. Peaks are exactly one; each stated fade boundary is 1%, not a
    // false/true bucket. Edge peaks at 0 and 100 are supported symmetrically.
    const std::array<VmGaussianProfile, 5> ratings{{
        {0.0, 0.0, 30.0}, {30.0, 10.0, 50.0}, {50.0, 30.0, 70.0},
        {75.0, 50.0, 90.0}, {100.0, 75.0, 100.0}}};
    const std::array<std::pair<double, double>, 5> boundaries{{
        {0.0, 30.0}, {10.0, 50.0}, {30.0, 70.0}, {50.0, 90.0}, {75.0, 100.0}}};
    for (std::size_t index = 0; index < ratings.size(); ++index) {
        assert(std::abs(VmFactStore::gaussianMembership(ratings[index].peak, ratings[index]) - 1.0) < 1e-12);
        const auto expectedAtLowerBoundary = boundaries[index].first == ratings[index].peak ? 1.0 : 0.01;
        const auto expectedAtUpperBoundary = boundaries[index].second == ratings[index].peak ? 1.0 : 0.01;
        assert(std::abs(VmFactStore::gaussianMembership(boundaries[index].first, ratings[index]) - expectedAtLowerBoundary) < 1e-12);
        assert(std::abs(VmFactStore::gaussianMembership(boundaries[index].second, ratings[index]) - expectedAtUpperBoundary) < 1e-12);
    }

    FelidaeIr fuzzy;
    fuzzy.registerCount = 6;
    fuzzy.constants = {encodeIrNumber(68.0), encodeIrNumber(75.0), encodeIrNumber(50.0), encodeIrNumber(90.0)};
    fuzzy.constantKinds = {IrConstantKind::Number, IrConstantKind::Number, IrConstantKind::Number, IrConstantKind::Number};
    fuzzy.words = {
        static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
        static_cast<IrWord>(IrOpcode::LoadConst), 1, 1,
        static_cast<IrWord>(IrOpcode::LoadConst), 2, 2,
        static_cast<IrWord>(IrOpcode::LoadConst), 3, 3,
        static_cast<IrWord>(IrOpcode::Membership), 4, 0, 1, 2, 3,
        static_cast<IrWord>(IrOpcode::Similarity), 5, 0, 1,
        static_cast<IrWord>(IrOpcode::Return), 4, 0,
        static_cast<IrWord>(IrOpcode::End)};
    IrVerifier::verify(fuzzy);
    const auto degree = std::get<VmDegree>(vm.execute(fuzzy, runtime, VmNil{}));
    assert(std::abs(degree.value - 0.696947396356321) < 1e-12);

    // Branching is a narrow control protocol, not a conversion of all values
    // into booleans. A number must continue through the non-false path, while
    // an explicit false value alone selects the alternate path.
    FelidaeIr numberBranch;
    numberBranch.registerCount = 2;
    numberBranch.constants = {encodeIrNumber(7.0), encodeIrNumber(11.0), encodeIrNumber(22.0)};
    numberBranch.constantKinds = {IrConstantKind::Number, IrConstantKind::Number, IrConstantKind::Number};
    numberBranch.words = {
        static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
        static_cast<IrWord>(IrOpcode::JumpIfFalse), 0, 11,
        static_cast<IrWord>(IrOpcode::LoadConst), 1, 1,
        static_cast<IrWord>(IrOpcode::Jump), 14,
        static_cast<IrWord>(IrOpcode::LoadConst), 1, 2,
        static_cast<IrWord>(IrOpcode::Return), 1, 0,
        static_cast<IrWord>(IrOpcode::End)};
    IrVerifier::verify(numberBranch);
    assert(std::get<double>(vm.execute(numberBranch, runtime, VmNil{})) == 11.0);

    auto falseBranch = numberBranch;
    falseBranch.constants = {0, encodeIrNumber(11.0), encodeIrNumber(22.0)};
    falseBranch.constantKinds = {IrConstantKind::Boolean, IrConstantKind::Number, IrConstantKind::Number};
    IrVerifier::verify(falseBranch);
    assert(std::get<double>(vm.execute(falseBranch, runtime, VmNil{})) == 22.0);

    // A single runtime result may retain crisp and soft data together. The
    // boolean comparison is data, rather than a whole-program truth value;
    // the fact and Degree retain their own typed semantics in the same array.
    FelidaeIr mixed;
    mixed.registerCount = 5;
    mixed.constants = {encodeIrNumber(68.0), encodeIrNumber(75.0), encodeIrNumber(50.0), encodeIrNumber(90.0), 1};
    mixed.constantKinds = {IrConstantKind::Number, IrConstantKind::Number, IrConstantKind::Number,
                           IrConstantKind::Number, IrConstantKind::Boolean};
    mixed.symbols = {symbolIdForName("Observation")};
    mixed.words = {
        static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
        static_cast<IrWord>(IrOpcode::LoadConst), 1, 1,
        static_cast<IrWord>(IrOpcode::LoadConst), 2, 2,
        static_cast<IrWord>(IrOpcode::LoadConst), 3, 3,
        static_cast<IrWord>(IrOpcode::Membership), 4, 0, 1, 2, 3,
        static_cast<IrWord>(IrOpcode::MakeFact), 0, 0,
        static_cast<IrWord>(IrOpcode::LoadConst), 1, 4,
        static_cast<IrWord>(IrOpcode::MakeArray), 2, 0, 3, 0, 1, 4,
        static_cast<IrWord>(IrOpcode::Return), 2, 0,
        static_cast<IrWord>(IrOpcode::End)};
    IrVerifier::verify(mixed);
    const auto mixedResult = std::get<VmArrayPtr>(vm.execute(mixed, runtime, VmNil{}));
    assert(mixedResult->values.size() == 3);
    assert(std::holds_alternative<VmFactPtr>(mixedResult->values[0]));
    assert(std::get<bool>(mixedResult->values[1]));
    assert(std::holds_alternative<VmDegree>(mixedResult->values[2]));

    // Training records persist stable semantic IDs and finite action kinds,
    // never VmValue's std::variant alternative order or a model-logit index.
    RuntimeTrainingRecord trainingRecord;
    trainingRecord.operationSymbol = mainSymbol;
    trainingRecord.inputKinds = {RuntimeValueKind::Text, RuntimeValueKind::Fact};
    trainingRecord.factTypes = {symbolIdForName("Observation"), symbolIdForName("Signal")};
    std::sort(trainingRecord.factTypes.begin(), trainingRecord.factTypes.end());
    trainingRecord.factTypeCounts = {{trainingRecord.factTypes[0], 2}, {trainingRecord.factTypes[1], 5}};
    trainingRecord.hierarchyEdges = {{symbolIdForName("Signal"), symbolIdForName("Observation")}};
    trainingRecord.targetKind = RuntimeTrainingTargetKind::FactFromInput;
    trainingRecord.targetValue = 0;
    const auto datasetPath = std::filesystem::temp_directory_path() / "felidae_runtime_schema_v6.jsonl";
    writeRuntimeTrainingDataset(datasetPath, std::span<const RuntimeTrainingRecord>{&trainingRecord, 1});
    const auto loadedRecords = loadRuntimeTrainingDataset(datasetPath);
    assert(loadedRecords.size() == 1);
    assert(loadedRecords.front().operationSymbol == mainSymbol);
    assert(loadedRecords.front().inputKinds == trainingRecord.inputKinds);
    assert(loadedRecords.front().factTypes == trainingRecord.factTypes);
    assert(loadedRecords.front().factTypeCounts == trainingRecord.factTypeCounts);
    assert(loadedRecords.front().hierarchyEdges == trainingRecord.hierarchyEdges);
    assert(loadedRecords.front().targetKind == RuntimeTrainingTargetKind::FactFromInput);
    assert(loadedRecords.front().targetValue == 0);
    bool retiredDatasetRejected = false;
    try {
        writeRuntimeTrainingDataset(std::filesystem::temp_directory_path() / "felidae_runtime_schema_v2.frtd",
                                    std::span<const RuntimeTrainingRecord>{&trainingRecord, 1});
    } catch (const IrError&) {
        retiredDatasetRejected = true;
    }
    assert(retiredDatasetRejected);
    std::filesystem::remove(datasetPath, ignored);
    std::filesystem::remove(path, ignored);
}
