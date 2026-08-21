#include "Symbol.h"
#include "form/BinaryIr.h"
#include "form/RegisterVm.h"
#include "form/RuntimeTraining.h"

#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using namespace Felidae;

RuntimeTrainingRecord makeIdentityTeacher(const VmValue& value,
                                          const VmKnowledgeSnapshot& knowledge) {
    RuntimeTrainingRecord record;
    // This is an explicit, stable operation ID. It is not inferred from the
    // binary, a program name, or a VmValue variant index.
    record.operationSymbol = symbolIdForName("fx.identity");
    record.inputKinds = {runtimeValueKind(value)};
    record.factTypes = knowledge.factTypes;
    record.factTypeCounts = knowledge.factTypeCounts;
    record.hierarchyEdges = knowledge.hierarchyEdges;
    if (std::holds_alternative<VmNil>(value)) {
        record.targetKind = RuntimeTrainingTargetKind::Nil;
    } else if (const auto boolean = std::get_if<bool>(&value)) {
        record.targetKind = RuntimeTrainingTargetKind::Boolean;
        record.targetValue = *boolean ? 1 : 0;
    } else if (std::holds_alternative<VmFactPtr>(value)) {
        // The runtime action clones and retains the input fact as a derived
        // fact, rather than losing fact identity by converting it to text.
        record.targetKind = RuntimeTrainingTargetKind::FactFromInput;
    } else {
        // Text, numbers, degrees, arrays, and maps remain their native VM
        // values through a checked bounded input reference.
        record.targetKind = RuntimeTrainingTargetKind::InputReference;
    }
    return record;
}

RuntimeTrainingRecord makeContextBooleanTeacher(std::string_view operation,
                                                const VmKnowledgeSnapshot& knowledge,
                                                bool value) {
    RuntimeTrainingRecord record;
    record.operationSymbol = symbolIdForName(operation);
    record.factTypes = knowledge.factTypes;
    record.factTypeCounts = knowledge.factTypeCounts;
    record.hierarchyEdges = knowledge.hierarchyEdges;
    record.targetKind = RuntimeTrainingTargetKind::Boolean;
    record.targetValue = value ? 1 : 0;
    return record;
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            throw std::runtime_error("usage: felidae_build_runtime_dataset output.jsonl program.bin [more-programs.bin]");
        }
        const std::filesystem::path output(argv[1]);
        if (output.extension() != ".jsonl") {
            throw std::runtime_error("runtime dataset output must be .jsonl");
        }
        std::vector<RuntimeTrainingRecord> records;
        records.reserve(static_cast<std::size_t>(argc - 2) * 3);
        for (int index = 2; index < argc; ++index) {
            const std::filesystem::path input(argv[index]);
            if (input.extension() != kBinaryIrExtension) {
                throw std::runtime_error("runtime dataset input must be .bin");
            }
            const auto module = loadBinaryIr(input);
            if (containsRuntimeSsmOperation(module)) {
                throw std::runtime_error("runtime SSM binaries require explicit operation-level teachers; no partial dataset was written");
            }
            try {
                FelidaeKnowledgeRuntime runtime;
                RegisterVm vm;
                const auto result = vm.executeMain(module, runtime);
                const auto knowledge = runtime.factStore()->knowledgeSnapshot();
                records.push_back(makeIdentityTeacher(result, knowledge));
                // These explicit SSM operation teachers are derived from the
                // same VM fact store and hierarchy snapshot inference sees.
                // They are not source syntax or a hidden second reasoner.
                records.push_back(makeContextBooleanTeacher("fx.context.has_facts", knowledge,
                                                             !knowledge.factTypes.empty()));
                records.push_back(makeContextBooleanTeacher("fx.context.has_hierarchy", knowledge,
                                                             !knowledge.hierarchyEdges.empty()));
            } catch (const std::exception& error) {
                throw std::runtime_error("runtime dataset input failed '" + input.string() + "': " + error.what());
            }
        }
        // The writer replaces the corpus only after every input has loaded,
        // verified, and executed successfully.
        writeRuntimeTrainingDataset(output, records);
        std::cout << records.size() << " deterministic runtime operation records written to "
                  << std::filesystem::absolute(output).lexically_normal().string() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
