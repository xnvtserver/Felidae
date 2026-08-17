#include "form/BinaryIr.h"
#include "Symbol.h"

#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <span>

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

    const auto path = std::filesystem::temp_directory_path() / "felidae_form_standalone.fir";
    writeBinaryIr(path, module);
    const auto loaded = loadBinaryIr(path);
    DirectVmRuntime runtime(loaded.procedures);
    RegisterVm vm;
    assert(std::get<double>(vm.executeMain(loaded, runtime)) == 42.0);

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
    DirectVmRuntime namedRuntime({{mainSymbol, namedProcedure}});
    const VmCallArgument namedArgument{publicName, 99.0};
    assert(std::get<double>(namedRuntime.callSymbolNamed(
        mainSymbol, std::span<const VmCallArgument>{&namedArgument, 1})) == 99.0);

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
        assert(std::abs(VmFactStore::gaussianMembership(boundaries[index].first, ratings[index]) - 0.01) < 1e-12);
        assert(std::abs(VmFactStore::gaussianMembership(boundaries[index].second, ratings[index]) - 0.01) < 1e-12);
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
    std::filesystem::remove(path);
}
