#include "form/BinaryIr.h"

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>

namespace {
bool rejects(const std::function<void()>& action) {
    try { action(); } catch (const Felidae::IrError&) { return true; }
    return false;
}
}

int main() {
    using namespace Felidae;
    const std::filesystem::path output(FELIDAE_TEST_OUTPUT_DIR);
    std::filesystem::create_directories(output);
    IrModule module;
    module.sentencePieceModelHash = "sha256:test-model";
    module.symbolTable = {{11, 12}};
    module.entryProcedure = 1;
    module.ir.registerCount = 1;
    module.ir.symbols = {1};
    module.ir.words = {static_cast<IrWord>(IrOpcode::Call), 0, 0, 0,
                       static_cast<IrWord>(IrOpcode::Return), 0, 0,
                       static_cast<IrWord>(IrOpcode::End)};
    IrProcedure procedure;
    procedure.ir.registerCount = 1;
    procedure.ir.constants = {encodeIrNumber(42.0)};
    procedure.ir.constantKinds = {IrConstantKind::Number};
    procedure.ir.words = {static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
                          static_cast<IrWord>(IrOpcode::Return), 0, 0,
                          static_cast<IrWord>(IrOpcode::End)};
    module.procedures.emplace(1, std::move(procedure));
    auto verified = verifyIrModule(std::move(module));
    const auto path = output / "felidae_ir_binary_test.bin";
    writeBinaryIr(path, verified);
    std::array<char, 8> magic{};
    { std::ifstream input(path, std::ios::binary); input.read(magic.data(), 8); }
    assert((magic == std::array<char, 8>{'F','E','L','B','I','R','\0','\0'}));
    const auto loaded = loadBinaryIr(path, "sha256:test-model");
    assert(loaded->symbolTable == verified->symbolTable);
    FelidaeKnowledgeRuntime runtime;
    assert(std::get<double>(RegisterVm{}.executeMain(loaded, runtime)) == 42.0);
    assert(rejects([&] { (void)loadBinaryIr(path, "sha256:wrong-model"); }));
    const auto malformed = output / "felidae_ir_malformed.bin";
    { std::ofstream out(malformed, std::ios::binary); out << "not FELBIR"; }
    assert(rejects([&] { (void)loadBinaryIr(malformed, "sha256:test-model"); }));
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(malformed, ignored);
    return 0;
}
