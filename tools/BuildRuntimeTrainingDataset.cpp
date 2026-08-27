#include "SentencePieceModel.h"
#include "form/BinaryIr.h"
#include "form/RegisterVm.h"
#include "form/RuntimeTraining.h"
#include "form/SemanticOperation.h"

#include <filesystem>
#include <iostream>
#include <vector>

namespace {
using namespace Felidae;

RuntimeTrainingRecord
makeIdentityTeacher(const VmValue &value, const VmKnowledgeSnapshot &knowledge,
                    std::span<const PieceSequence> symbolTable) {
  RuntimeTrainingRecord record;
  // This is an explicit, stable operation ID. It is not inferred from the
  // binary, a program name, or a VmValue variant index.
  record.operationId =
      static_cast<std::uint16_t>(SemanticOperationId::Identity);
  record.inputKinds = {runtimeValueKind(value)};
  auto encoded = runtimeKnowledgePieces(knowledge, symbolTable);
  record.factTypes = std::move(encoded.factTypes);
  record.factTypeCounts = std::move(encoded.factTypeCounts);
  record.hierarchyEdges = std::move(encoded.hierarchyEdges);
  // Identity always preserves the exact typed input. Fact derivation has a
  // different permanent operation ID and must never be smuggled in here.
  record.targetKind = RuntimeTrainingTargetKind::InputReference;
  record.targetValue = 0;
  return record;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 3) {
      throw std::runtime_error("usage: felidae_build_runtime_dataset "
                               "output.jsonl program.bin [more-programs.bin]");
    }
    const std::filesystem::path output(argv[1]);
    if (output.extension() != ".jsonl") {
      throw std::runtime_error("runtime dataset output must be .jsonl");
    }
    std::vector<RuntimeTrainingRecord> records;
    records.reserve(static_cast<std::size_t>(argc - 2));
    for (int index = 2; index < argc; ++index) {
      const std::filesystem::path input(argv[index]);
      if (input.extension() != kBinaryIrExtension) {
        throw std::runtime_error("runtime dataset input must be .bin");
      }
      const auto module =
          loadBinaryIr(input, felidaeSentencePieceModelIdentity());
      if (containsRuntimeSemanticOperation(module)) {
        throw std::runtime_error(
            "runtime SSM binaries require explicit operation-level teachers; "
            "no partial dataset was written");
      }
      try {
        FelidaeKnowledgeRuntime runtime;
        RegisterVm vm;
        const auto result = vm.executeMain(module, runtime);
        const auto knowledge = runtime.factStore()->knowledgeSnapshot();
        records.push_back(
            makeIdentityTeacher(result, knowledge, module->symbolTable));
      } catch (const std::exception &error) {
        throw std::runtime_error("runtime dataset input failed '" +
                                 input.string() + "': " + error.what());
      }
    }
    // The writer replaces the corpus only after every input has loaded,
    // verified, and executed successfully.
    writeRuntimeTrainingDataset(output, records);
    std::cout << records.size()
              << " deterministic runtime operation records written to "
              << std::filesystem::absolute(output).lexically_normal().string()
              << "\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
