#include "form/BinaryIr.h"
#include "form/RuntimeTraining.h"

#include <filesystem>
#include <iostream>

namespace {
Felidae::IrWord valueKind(const Felidae::VmValue& value) {
    return static_cast<Felidae::IrWord>(value.index());
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: felidae_build_runtime_dataset program.fir output.frtd");
        const std::filesystem::path input(argv[1]);
        if (input.extension() != Felidae::kBinaryIrExtension) throw std::runtime_error("runtime dataset input must be .bin");
        const auto module = Felidae::loadBinaryIr(input);
        Felidae::FelidaeKnowledgeRuntime runtime;
        Felidae::RegisterVm vm;
        const auto result = vm.executeMain(module, runtime);
        Felidae::RuntimeTrainingRecord record;
        record.moduleEntry = module.entryProcedure;
        record.inputKind = valueKind(Felidae::VmNil{});
        record.resultKind = valueKind(result);
        for (const auto& fact : runtime.factStore()->snapshot()) {
            record.relevantFacts.push_back(fact->id);
            record.factTypes.push_back(fact->type);
        }
        record.trace = runtime.executionTraces();
        Felidae::writeRuntimeTrainingDataset(argv[2], std::span<const Felidae::RuntimeTrainingRecord>(&record, 1));
        std::cout << std::filesystem::absolute(argv[2]).lexically_normal().string() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
