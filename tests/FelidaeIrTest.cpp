#include "form/IrModule.h"
#include "form/RegisterVm.h"
#include "form/libs/Builtin.h"

#include <algorithm>
#include <array>
#include <barrier>
#include <cassert>
#include <cmath>
#include <functional>
#include <limits>
#include <thread>
#include <vector>

namespace {
Felidae::Form::BuiltinTextCodec testTextCodec() {
  return {[](std::span<const Felidae::PieceId> pieces) {
            std::string text;
            text.reserve(pieces.size());
            for (const auto piece : pieces)
              text.push_back(static_cast<char>(piece));
            return text;
          },
          [](std::string_view text) {
            Felidae::PieceSequence pieces;
            pieces.reserve(text.size());
            for (const unsigned char character : text)
              pieces.push_back(character);
            return pieces;
          }};
}

Felidae::VmText testText(std::string_view text) {
  return Felidae::VmText{testTextCodec().encode(text)};
}

bool rejects(const std::function<void()> &action) {
  try {
    action();
  } catch (const Felidae::IrError &) {
    return true;
  }
  return false;
}

Felidae::IrModule returning(Felidae::IrConstant value,
                            Felidae::IrConstantKind kind) {
  using namespace Felidae;
  IrModule module;
  module.sentencePieceModelIdentity = "sha256:test";
  module.symbolTable = {{1}};
  module.entryProcedure = 1;
  module.ir.registerCount = 1;
  module.ir.symbols = {1};
  module.ir.words = {static_cast<IrWord>(IrOpcode::Call),
                     0,
                     0,
                     0,
                     static_cast<IrWord>(IrOpcode::Return),
                     0,
                     0,
                     static_cast<IrWord>(IrOpcode::End)};
  IrProcedure procedure;
  procedure.ir.registerCount = 1;
  procedure.ir.constants = {{kind, value}};
  procedure.ir.words = {static_cast<IrWord>(IrOpcode::LoadConst), 0, 0,
                        static_cast<IrWord>(IrOpcode::Return),    0, 0,
                        static_cast<IrWord>(IrOpcode::End)};
  module.procedures.emplace(1, std::move(procedure));
  return module;
}

Felidae::IrModule numeric(Felidae::NumericOperation operation,
                          const std::vector<double> &operands) {
  using namespace Felidae;
  IrModule module;
  module.sentencePieceModelIdentity = "sha256:test";
  module.symbolTable = {{1}};
  module.entryProcedure = 1;
  module.ir.registerCount = 1;
  module.ir.symbols = {1};
  module.ir.words = {static_cast<IrWord>(IrOpcode::Call),
                     0,
                     0,
                     0,
                     static_cast<IrWord>(IrOpcode::Return),
                     0,
                     0,
                     static_cast<IrWord>(IrOpcode::End)};
  IrProcedure procedure;
  procedure.ir.registerCount = operands.size() + 1;
  for (std::size_t index = 0; index < operands.size(); ++index) {
    procedure.ir.constants.push_back(
        {IrConstantKind::Number, encodeIrNumber(operands[index])});
    procedure.ir.words.insert(procedure.ir.words.end(),
                              {static_cast<IrWord>(IrOpcode::LoadConst),
                               static_cast<IrWord>(index),
                               static_cast<IrWord>(index)});
  }
  const auto result = static_cast<IrWord>(operands.size());
  procedure.ir.words.insert(procedure.ir.words.end(),
                            {static_cast<IrWord>(IrOpcode::Numeric), result,
                             static_cast<IrWord>(operation),
                             static_cast<IrWord>(operands.size())});
  for (std::size_t index = 0; index < operands.size(); ++index)
    procedure.ir.words.push_back(static_cast<IrWord>(index));
  procedure.ir.words.insert(procedure.ir.words.end(),
                            {static_cast<IrWord>(IrOpcode::Return), result, 0,
                             static_cast<IrWord>(IrOpcode::End)});
  module.procedures.emplace(1, std::move(procedure));
  return module;
}

Felidae::IrModule builtin(Felidae::BuiltinId operation,
                          std::string_view input) {
  using namespace Felidae;
  IrModule module;
  module.sentencePieceModelIdentity = "sha256:test";
  module.symbolTable = {{1}};
  module.entryProcedure = 1;
  module.ir.registerCount = 1;
  module.ir.symbols = {1};
  module.ir.words = {static_cast<IrWord>(IrOpcode::Call),
                     0,
                     0,
                     0,
                     static_cast<IrWord>(IrOpcode::Return),
                     0,
                     0,
                     static_cast<IrWord>(IrOpcode::End)};
  IrProcedure procedure;
  procedure.ir.registerCount = 2;
  procedure.ir.constants = {{IrConstantKind::Text, 0}};
  procedure.ir.texts = {testText(input).pieces};
  procedure.ir.words = {static_cast<IrWord>(IrOpcode::LoadConst),
                        0,
                        0,
                        static_cast<IrWord>(IrOpcode::Builtin),
                        1,
                        static_cast<IrWord>(operation),
                        1,
                        0,
                        static_cast<IrWord>(IrOpcode::Return),
                        1,
                        0,
                        static_cast<IrWord>(IrOpcode::End)};
  module.procedures.emplace(1, std::move(procedure));
  return module;
}

double executeNumeric(Felidae::NumericOperation operation,
                      std::vector<double> operands) {
  using namespace Felidae;
  auto module = verifyIrModule(numeric(operation, operands));
  FelidaeKnowledgeRuntime runtime;
  return std::get<double>(RegisterVm{}.executeMain(module, runtime));
}

Felidae::IrModule tensor(Felidae::TensorOperation operation,
                         const std::vector<double> &operands) {
  using namespace Felidae;
  auto module = numeric(NumericOperation::Min, operands);
  auto &words = module.procedures.at(1).ir.words;
  const auto instruction = std::find(words.begin(), words.end(),
                                     static_cast<IrWord>(IrOpcode::Numeric));
  assert(instruction != words.end());
  *instruction = static_cast<IrWord>(IrOpcode::Tensor);
  *(instruction + 2) = static_cast<IrWord>(operation);
  return module;
}

double executeModulo(double left, double right) {
  using namespace Felidae;
  auto module = numeric(NumericOperation::Diff, {left, right});
  auto &program = module.procedures.at(1).ir;
  program.words = {
      static_cast<IrWord>(IrOpcode::LoadConst),
      0,
      0,
      static_cast<IrWord>(IrOpcode::LoadConst),
      1,
      1,
      static_cast<IrWord>(IrOpcode::Mod),
      2,
      0,
      1,
      static_cast<IrWord>(IrOpcode::Return),
      2,
      0,
      static_cast<IrWord>(IrOpcode::End),
  };
  auto verified = verifyIrModule(std::move(module));
  FelidaeKnowledgeRuntime runtime;
  return std::get<double>(RegisterVm{}.executeMain(verified, runtime));
}

double executeKeyedEquality(bool facts) {
  using namespace Felidae;
  auto module = returning(encodeIrNumber(1.0), IrConstantKind::Number);
  module.symbolTable = {{1}, {2}, {3}};
  auto &program = module.procedures.at(1).ir;
  program.registerCount = 5;
  program.constants = {
      {IrConstantKind::Number, encodeIrNumber(10.0)},
      {IrConstantKind::Number, encodeIrNumber(20.0)},
  };
  program.symbols = {1, 2, 3};
  program.words = {
      static_cast<IrWord>(IrOpcode::LoadConst), 2, 0,
      static_cast<IrWord>(IrOpcode::LoadConst), 3, 1,
  };
  if (facts) {
    program.words.insert(program.words.end(),
                         {static_cast<IrWord>(IrOpcode::MakeFact),
                          0,
                          0,
                          static_cast<IrWord>(IrOpcode::SetField),
                          0,
                          1,
                          2,
                          static_cast<IrWord>(IrOpcode::SetField),
                          0,
                          2,
                          3,
                          static_cast<IrWord>(IrOpcode::MakeFact),
                          1,
                          0,
                          static_cast<IrWord>(IrOpcode::SetField),
                          1,
                          2,
                          3,
                          static_cast<IrWord>(IrOpcode::SetField),
                          1,
                          1,
                          2});
  } else {
    program.words.insert(program.words.end(),
                         {static_cast<IrWord>(IrOpcode::MakeMap), 0, 0, 2, 1, 2,
                          2, 3, static_cast<IrWord>(IrOpcode::MakeMap), 1, 0, 2,
                          2, 3, 1, 2});
  }
  program.words.insert(program.words.end(),
                       {static_cast<IrWord>(IrOpcode::Compare), 4, 0, 1,
                        static_cast<IrWord>(IrComparison::Equal),
                        static_cast<IrWord>(IrOpcode::Return), 4, 0,
                        static_cast<IrWord>(IrOpcode::End)});
  auto verified = verifyIrModule(std::move(module));
  FelidaeKnowledgeRuntime runtime;
  return std::get<double>(RegisterVm{}.executeMain(verified, runtime));
}

bool close(double actual, double expected, double tolerance = 1e-12) {
  return std::abs(actual - expected) <=
         tolerance * std::max({1.0, std::abs(actual), std::abs(expected)});
}
} // namespace

int main() {
  using namespace Felidae;
  VmRuntime unsupportedRuntime;
  IrModule unsupportedModule;
  assert(
      rejects([&] { unsupportedRuntime.installIrModule(unsupportedModule); }));
  assert(rejects([&] { (void)unsupportedRuntime.resolveSymbol(1); }));
  assert(rejects(
      [&] { unsupportedRuntime.retainFact(std::make_shared<VmFact>()); }));
  assert(rejects([&] {
    unsupportedRuntime.mutateFact(std::make_shared<VmFact>(), 1, 1.0);
  }));
  assert(rejects([&] { unsupportedRuntime.registerFactType(1, {}); }));

  VmFactStore hierarchy;
  hierarchy.registerType(1, {});
  hierarchy.registerType(2, {1});
  hierarchy.registerType(3, {2});
  hierarchy.registerType(3, {2});
  assert(rejects([&] { hierarchy.registerType(4, {1, 1}); }));
  VmFactStore cyclicHierarchy;
  cyclicHierarchy.registerType(1, {2});
  cyclicHierarchy.registerType(2, {3});
  assert(rejects([&] { cyclicHierarchy.registerType(3, {1}); }));
  auto indexedFactBuilder = std::make_shared<VmFact>();
  indexedFactBuilder->type = 3;
  indexedFactBuilder->fields = {{10, 42.0}};
  const auto indexedFact = hierarchy.retain(indexedFactBuilder);
  auto secondIndexedFactBuilder = std::make_shared<VmFact>();
  secondIndexedFactBuilder->type = 2;
  secondIndexedFactBuilder->fields = {{10, 43.0}};
  const auto secondIndexedFact = hierarchy.retain(secondIndexedFactBuilder);
  assert(hierarchy.retain(secondIndexedFact) == secondIndexedFact);
  VmFactStore unrelatedStore;
  assert(rejects([&] { (void)unrelatedStore.retain(secondIndexedFact); }));
  const auto assignable = hierarchy.snapshotAssignableTo(1);
  assert(assignable.size() == 2);
  assert(assignable[0]->id < assignable[1]->id);
  assert((hierarchy.hierarchyProof(3, 1) == std::vector<IrSymbolRef>{3, 2, 1}));
  assert((hierarchy.commonAncestors(2, 3) == std::vector<IrSymbolRef>{1, 2}));
  assert((hierarchy.leastCommonAncestors(2, 3) == std::vector<IrSymbolRef>{2}));
  assert((hierarchy.mostGeneralCommonAncestors(2, 3) ==
          std::vector<IrSymbolRef>{1}));
  // Prime the per-type closure before adding type 5. Registering hierarchy
  // edges must invalidate only that cache domain and expose the new path.
  assert(hierarchy.hierarchyProof(5, 1).empty());
  hierarchy.registerType(4, {1});
  hierarchy.registerType(5, {2, 4});
  assert((hierarchy.hierarchyProof(5, 1) == std::vector<IrSymbolRef>{5, 2, 1}));
  assert(hierarchy.snapshotByField(10).size() == 2);
  auto invalidFact = std::make_shared<VmFact>();
  invalidFact->type = 0;
  assert(rejects([&] { (void)hierarchy.retain(invalidFact); }));
  invalidFact->type = 3;
  invalidFact->fields = {{10, 1.0}, {10, 2.0}};
  assert(rejects([&] { (void)hierarchy.retain(invalidFact); }));
  assert(rejects([&] { hierarchy.mutate(indexedFact, 11, VmFactPtr{}, 0); }));
  const auto revisionsBeforeMutation = hierarchy.revisions();
  std::uint64_t knowledgeRevision =
      std::numeric_limits<std::uint64_t>::max();
  VmKnowledgeSnapshot knowledgeBeforeMutation;
  hierarchy.refreshKnowledgeSnapshot(knowledgeRevision,
                                     knowledgeBeforeMutation);
  const auto knowledgeRevisionBeforeMutation = knowledgeRevision;
  const auto updatedIndexedFact = hierarchy.mutate(indexedFact, 10, 44.0, 0);
  const auto revisionsAfterMutation = hierarchy.revisions();
  assert(revisionsAfterMutation.hierarchy ==
         revisionsBeforeMutation.hierarchy);
  assert(revisionsAfterMutation.membership ==
         revisionsBeforeMutation.membership);
  assert(revisionsAfterMutation.content ==
         revisionsBeforeMutation.content + 1);
  VmKnowledgeSnapshot knowledgeAfterMutation = knowledgeBeforeMutation;
  hierarchy.refreshKnowledgeSnapshot(knowledgeRevision,
                                     knowledgeAfterMutation);
  assert(knowledgeRevision == knowledgeRevisionBeforeMutation);
  assert(knowledgeAfterMutation.factTypes ==
         knowledgeBeforeMutation.factTypes);
  assert(knowledgeAfterMutation.factTypeCounts ==
         knowledgeBeforeMutation.factTypeCounts);
  assert(knowledgeAfterMutation.hierarchyEdges ==
         knowledgeBeforeMutation.hierarchyEdges);
  assert(std::get<double>(indexedFact->fields.front().second) == 42.0);
  assert(std::get<double>(updatedIndexedFact->fields.front().second) == 44.0);
  assert(hierarchy.snapshotByField(10).front() == updatedIndexedFact);
  assert(hierarchy.snapshotAssignableTo(1).front() == updatedIndexedFact);
  assert(rejects([&] { (void)hierarchy.mutate(indexedFact, 10, 45.0, 0); }));

  VmFactStore concurrentFacts;
  concurrentFacts.registerType(20, {});
  auto concurrentBuilder = std::make_shared<VmFact>();
  concurrentBuilder->type = 20;
  concurrentBuilder->fields = {{21, 1.0}};
  const auto immutableSnapshot = concurrentFacts.retain(concurrentBuilder);
  std::barrier concurrentStart(2);
  std::thread reader([&] {
    concurrentStart.arrive_and_wait();
    for (std::size_t iteration = 0; iteration < 256; ++iteration) {
      // This handle remains readable while the store publishes a replacement.
      assert(std::get<double>(immutableSnapshot->fields.front().second) == 1.0);
      const auto indexed = concurrentFacts.snapshotByField(21);
      assert(indexed.size() == 1);
      const auto value = std::get<double>(indexed.front()->fields.front().second);
      assert(value == 1.0 || value == 2.0);
      assert((concurrentFacts.hierarchyProof(20, 20) ==
              std::vector<IrSymbolRef>{20}));
    }
  });
  concurrentStart.arrive_and_wait();
  const auto concurrentUpdate =
      concurrentFacts.mutate(immutableSnapshot, 21, 2.0, 22);
  reader.join();
  assert(std::get<double>(immutableSnapshot->fields.front().second) == 1.0);
  assert(std::get<double>(concurrentUpdate->fields.front().second) == 2.0);
  assert(concurrentFacts.snapshotByField(21) ==
         std::vector<VmFactPtr>{concurrentUpdate});
  assert(concurrentFacts.mutations().size() == 1);
  assert(concurrentFacts.provenance().size() == 2);

  FelidaeKnowledgeRuntime bindings;
  IrModule bindingModule;
  bindingModule.symbolTable = {{1}, {2}, {3}};
  bindings.installIrModule(bindingModule);
  bindings.storeSymbol(1, 42.0);
  assert(std::get<double>(bindings.loadSymbol(1)) == 42.0);
  assert(rejects([&] { bindings.storeSymbol(1, 43.0); }));
  bindings.enterProcedure(2, std::array<IrSymbolRef, 1>{2},
                          std::array<VmValue, 1>{7.0});
  assert(std::get<double>(bindings.loadSymbol(2)) == 7.0);
  assert(std::get<double>(bindings.loadSymbol(1)) == 42.0);
  bindings.storeSymbol(3, 9.0);
  assert(std::get<double>(bindings.loadSymbol(3)) == 9.0);
  assert(rejects([&] { bindings.storeSymbol(2, 8.0); }));
  bindings.leaveProcedure();
  assert(rejects([&] { (void)bindings.loadSymbol(3); }));
  assert(rejects([&] { (void)bindings.resolveSymbol(4); }));

  FelidaeKnowledgeRuntime persistentSymbols;
  IrModule firstSymbols;
  firstSymbols.symbolTable = {{11}, {12, 13}};
  persistentSymbols.installIrModule(firstSymbols);
  const auto firstType = persistentSymbols.resolveSymbol(1);
  const auto sharedField = persistentSymbols.resolveSymbol(2);
  persistentSymbols.registerFactType(firstType, {});
  auto firstFact = std::make_shared<VmFact>();
  firstFact->type = firstType;
  firstFact->fields = {{sharedField, 1.0}};
  const auto retainedFirstFact = persistentSymbols.retainFact(firstFact);

  IrModule secondSymbols;
  secondSymbols.symbolTable = {{21}, {12, 13}};
  persistentSymbols.installIrModule(secondSymbols);
  const auto secondType = persistentSymbols.resolveSymbol(1);
  assert(secondType != firstType);
  assert(persistentSymbols.resolveSymbol(2) == sharedField);
  persistentSymbols.registerFactType(secondType, {});
  auto secondFact = std::make_shared<VmFact>();
  secondFact->type = secondType;
  secondFact->fields = {{sharedField, 2.0}};
  const auto retainedSecondFact = persistentSymbols.retainFact(secondFact);
  assert(persistentSymbols.snapshotFacts(firstType) ==
         std::vector<VmFactPtr>{retainedFirstFact});
  assert(persistentSymbols.snapshotFacts(secondType) ==
         std::vector<VmFactPtr>{retainedSecondFact});

  const auto textCodec = testTextCodec();
  const std::vector<PieceSequence> noSymbols;
  const auto parsed = Form::evaluateBuiltin(
      BuiltinId::JsonParse,
      std::array<VmValue, 1>{testText("{\"name\":\"Ada\",\"active\":true}")},
      noSymbols, textCodec);
  assert(std::holds_alternative<VmTextMapPtr>(parsed));
  const auto name = Form::evaluateBuiltin(
      BuiltinId::JsonGet, std::array<VmValue, 2>{parsed, testText("name")},
      noSymbols, textCodec);
  assert(std::get<VmText>(name).pieces == testText("Ada").pieces);
  assert(std::get<double>(Form::evaluateBuiltin(
             BuiltinId::JsonHas,
             std::array<VmValue, 2>{parsed, testText("active")}, noSymbols,
             textCodec)) == 1.0);
  const auto keys =
      Form::evaluateBuiltin(BuiltinId::JsonKeys, std::array<VmValue, 1>{parsed},
                            noSymbols, textCodec);
  assert(std::get<VmArrayPtr>(keys)->values.size() == 2);
  const auto updated = Form::evaluateBuiltin(
      BuiltinId::JsonSet,
      std::array<VmValue, 3>{parsed, testText("score"), VmValue{42.0}},
      noSymbols, textCodec);
  const auto removed =
      Form::evaluateBuiltin(BuiltinId::JsonRemove,
                            std::array<VmValue, 2>{updated, testText("active")},
                            noSymbols, textCodec);
  assert(std::get<double>(Form::evaluateBuiltin(
             BuiltinId::JsonHas,
             std::array<VmValue, 2>{removed, testText("active")}, noSymbols,
             textCodec)) == 0.0);
  const auto jsonText = Form::evaluateBuiltin(BuiltinId::JsonToText,
                                              std::array<VmValue, 1>{removed},
                                              noSymbols, textCodec);
  assert(testTextCodec().decode(std::get<VmText>(jsonText).pieces) ==
         "{\"name\":\"Ada\",\"score\":42.0}");
  const auto csvRows = Form::evaluateBuiltin(
      BuiltinId::CsvParse,
      std::array<VmValue, 1>{testText("name,score\nAda,42\n")}, noSymbols,
      textCodec);
  assert(std::get<VmArrayPtr>(csvRows)->values.size() == 1);
  assert(std::get<double>(Form::evaluateBuiltin(
             BuiltinId::ArrayLen, std::array<VmValue, 1>{csvRows}, noSymbols,
             textCodec)) == 1.0);
  assert(std::get<VmTextMapPtr>(Form::evaluateBuiltin(
             BuiltinId::ArrayGet,
             std::array<VmValue, 2>{csvRows, VmValue{0.0}}, noSymbols,
             textCodec))
             ->entries.size() == 2);
  auto aggregateValues = std::make_shared<VmArray>();
  aggregateValues->values = {2.0, 4.0, 6.0};
  assert(std::get<double>(Form::evaluateBuiltin(
             BuiltinId::Sum, std::array<VmValue, 1>{aggregateValues},
             noSymbols, textCodec)) == 12.0);
  assert(std::get<double>(Form::evaluateBuiltin(
             BuiltinId::Average, std::array<VmValue, 1>{aggregateValues},
             noSymbols, textCodec)) == 4.0);
  const auto csvText = Form::evaluateBuiltin(BuiltinId::CsvToText,
                                             std::array<VmValue, 1>{csvRows},
                                             noSymbols, textCodec);
  assert(testTextCodec().decode(std::get<VmText>(csvText).pieces) ==
         "name,score\nAda,42\n");
  assert(rejects([&] {
    (void)Form::evaluateBuiltin(
        BuiltinId::CsvToFacts,
        std::array<VmValue, 2>{testText("name,score\nAda,42\n"),
                               testText("Person")},
        noSymbols, textCodec);
  }));
  const auto felidaeFacts = Form::evaluateBuiltin(
      BuiltinId::CsvToFelidaeFacts,
      std::array<VmValue, 2>{csvRows, testText("Person")}, noSymbols,
      textCodec);
  assert(testTextCodec().decode(std::get<VmText>(felidaeFacts).pieces) ==
         "Person(name: \"Ada\", score: \"42\")\n");

  const auto groupData = Form::evaluateBuiltin(
      BuiltinId::JsonParse,
      std::array<VmValue, 1>{testText(
          R"({"set":[0,1],"table":[{"left":0,"right":0,"result":0},{"left":0,"right":1,"result":1},{"left":1,"right":0,"result":1},{"left":1,"right":1,"result":0}]})")},
      noSymbols, textCodec);
  const auto groupSet = Form::evaluateBuiltin(
      BuiltinId::JsonGet, std::array<VmValue, 2>{groupData, testText("set")},
      noSymbols, textCodec);
  const auto groupTable = Form::evaluateBuiltin(
      BuiltinId::JsonGet, std::array<VmValue, 2>{groupData, testText("table")},
      noSymbols, textCodec);
  for (const auto operation :
       {BuiltinId::GroupClosed, BuiltinId::GroupAssociative,
        BuiltinId::GroupCommutative}) {
    assert(std::get<double>(Form::evaluateBuiltin(
               operation, std::array<VmValue, 2>{groupSet, groupTable},
               noSymbols, textCodec)) == 1.0);
  }
  for (const auto operation :
       {BuiltinId::GroupIdentity, BuiltinId::GroupInverse,
        BuiltinId::GroupAbelian}) {
    assert(std::get<double>(Form::evaluateBuiltin(
               operation,
               std::array<VmValue, 3>{groupSet, groupTable, VmValue{0.0}},
               noSymbols, textCodec)) == 1.0);
  }
  const auto groupValidation = Form::evaluateBuiltin(
      BuiltinId::GroupValidate,
      std::array<VmValue, 3>{groupSet, groupTable, VmValue{0.0}}, noSymbols,
      textCodec);
  assert(std::get<double>(Form::evaluateBuiltin(
             BuiltinId::JsonGet,
             std::array<VmValue, 2>{groupValidation, testText("valid")},
             noSymbols, textCodec)) == 1.0);
  assert(rejects([&] {
    auto empty = std::make_shared<VmArray>();
    (void)Form::evaluateBuiltin(
        BuiltinId::GroupClosed,
        std::array<VmValue, 2>{empty, std::make_shared<VmArray>()}, noSymbols,
        textCodec);
  }));
  assert(rejects([&] {
    auto duplicateMembers = std::make_shared<VmArray>();
    duplicateMembers->values = {0.0, 0.0};
    (void)Form::evaluateBuiltin(
        BuiltinId::GroupClosed,
        std::array<VmValue, 2>{duplicateMembers, groupTable}, noSymbols,
        textCodec);
  }));

  const auto setInputs = Form::evaluateBuiltin(
      BuiltinId::JsonParse, std::array<VmValue, 1>{testText("[[1,2,2],[2,3]]")},
      noSymbols, textCodec);
  const auto fields = Form::evaluateBuiltin(
      BuiltinId::JsonParse, std::array<VmValue, 1>{testText("[]")}, noSymbols,
      textCodec);
  const auto setResult = [&](BuiltinId operation,
                             std::initializer_list<VmValue> inputs) {
    return Form::evaluateBuiltin(
        operation, std::span<const VmValue>{inputs.begin(), inputs.size()},
        noSymbols, textCodec);
  };
  assert(std::get<VmArrayPtr>(setResult(BuiltinId::SetUnion, {setInputs}))
             ->values.size() == 3);
  assert(
      std::get<VmArrayPtr>(setResult(BuiltinId::SetIntersection, {setInputs}))
          ->values.size() == 1);
  assert(std::get<VmArrayPtr>(setResult(BuiltinId::SetDifference, {setInputs}))
             ->values.size() == 1);
  assert(std::get<VmArrayPtr>(
             setResult(BuiltinId::SetSymmetricDifference, {setInputs}))
             ->values.size() == 2);
  assert(std::get<double>(setResult(BuiltinId::SetEquals, {setInputs})) == 0.0);
  assert(std::get<double>(setResult(BuiltinId::SetSubset, {setInputs})) == 0.0);
  assert(std::get<double>(setResult(BuiltinId::SetSuperset, {setInputs})) ==
         0.0);
  assert(std::get<double>(setResult(BuiltinId::SetDisjoint, {setInputs})) ==
         0.0);
  const auto firstSet = Form::evaluateBuiltin(
      BuiltinId::JsonParse, std::array<VmValue, 1>{testText("[1,2,2]")},
      noSymbols, textCodec);
  assert(std::get<double>(setResult(BuiltinId::SetCardinality, {firstSet})) ==
         2.0);
  assert(std::get<double>(setResult(BuiltinId::SetContains,
                                    {firstSet, VmValue{2.0}})) == 1.0);
  for (const auto operation :
       {BuiltinId::SetIntersectionBy, BuiltinId::SetDifferenceBy,
        BuiltinId::SetSymmetricDifferenceBy, BuiltinId::SetEqualsBy,
        BuiltinId::SetSubsetBy, BuiltinId::SetDisjointBy}) {
    assert(!std::holds_alternative<VmNil>(
        setResult(operation, {setInputs, fields})));
  }
  const auto objectSet = Form::evaluateBuiltin(
      BuiltinId::JsonParse,
      std::array<VmValue, 1>{testText(R"([{"id":1},{"id":2}])")}, noSymbols,
      textCodec);
  const auto idField = Form::evaluateBuiltin(
      BuiltinId::JsonParse, std::array<VmValue, 1>{testText("[\"id\"]")},
      noSymbols, textCodec);
  assert(std::get<double>(setResult(BuiltinId::SetContainsBy,
                                    {objectSet, VmValue{2.0}, idField})) ==
         1.0);
  assert(rejects([&] {
    (void)Form::evaluateBuiltin(BuiltinId::JsonParse,
                                std::array<VmValue, 1>{testText("{invalid")},
                                noSymbols, textCodec);
  }));

  auto parsedByVm = verifyIrModule(
      builtin(BuiltinId::JsonParse, "{\"active\":true,\"score\":42}"));
  FelidaeKnowledgeRuntime builtinRuntime(nullptr, 1024, 256, {}, nullptr,
                                         textCodec.decode, textCodec.encode);
  const auto vmObject = RegisterVm{}.executeMain(parsedByVm, builtinRuntime);
  assert(std::holds_alternative<VmTextMapPtr>(vmObject));
  assert(rejects([&] {
    FelidaeKnowledgeRuntime missingCodec;
    (void)RegisterVm{}.executeMain(parsedByVm, missingCodec);
  }));

  static_assert(sizeof(IrWord) == 4);
  static_assert(sizeof(IrConstant) == 8);
  FelidaeKnowledgeRuntime runtime;
  auto number =
      verifyIrModule(returning(encodeIrNumber(42.0), IrConstantKind::Number));
  assert(std::get<double>(RegisterVm{}.executeMain(number, runtime)) == 42.0);
  assert(executeKeyedEquality(false) == 1.0);
  assert(executeKeyedEquality(true) == 1.0);

  FelidaeKnowledgeRuntime truthRuntime;
  auto truth = verifyIrModule(returning(1, IrConstantKind::Boolean));
  const auto truthValue = RegisterVm{}.executeMain(truth, truthRuntime);
  assert(std::holds_alternative<double>(truthValue));
  assert(std::get<double>(truthValue) == 1.0);
  assert(vmValueToDisplayString(truthValue) == "1.0");

  FelidaeKnowledgeRuntime falseRuntime;
  auto falseTruth = verifyIrModule(returning(0, IrConstantKind::Boolean));
  const auto falseValue = RegisterVm{}.executeMain(falseTruth, falseRuntime);
  assert(std::holds_alternative<double>(falseValue));
  assert(std::get<double>(falseValue) == 0.0);
  assert(vmValueToDisplayString(falseValue) == "0.0");

  assert(numericOperationForName("min") == NumericOperation::Min);
  assert(numericOperationForName("weighted_avg") ==
         NumericOperation::WeightedAverage);
  assert(numericOperationForName("in_range") == NumericOperation::InRange);
  assert(!numericOperationForName("MIN"));
  assert(close(executeNumeric(NumericOperation::Min, {0.8, 0.3}), 0.3));
  assert(close(executeNumeric(NumericOperation::Max, {0.8, 0.3}), 0.8));
  assert(close(executeNumeric(NumericOperation::Abs, {-4.5}), 4.5));
  assert(close(executeNumeric(NumericOperation::Diff, {8.2, 5.0}), 3.2));
  assert(close(executeNumeric(NumericOperation::Average, {10, 20}), 15));
  assert(close(executeNumeric(NumericOperation::Average,
                              {std::numeric_limits<double>::max(),
                               std::numeric_limits<double>::max()}),
               std::numeric_limits<double>::max()));
  assert(close(
      executeNumeric(NumericOperation::WeightedAverage, {10, 20, 1, 3}), 17.5));
  assert(close(executeNumeric(NumericOperation::Clamp, {1.4, 0, 1}), 1));
  assert(close(executeNumeric(NumericOperation::Clamp, {-0.2, 0, 1}), 0));
  assert(close(executeNumeric(NumericOperation::Floor, {-4.2}), -5));
  assert(close(executeNumeric(NumericOperation::Ceil, {-4.8}), -4));
  assert(close(executeNumeric(NumericOperation::Round, {4.6}), 5));
  assert(close(executeNumeric(NumericOperation::Round, {-4.6}), -5));
  assert(close(executeNumeric(NumericOperation::Trunc, {-4.8}), -4));
  assert(close(executeNumeric(NumericOperation::Sqrt, {9}), 3));
  assert(close(executeNumeric(NumericOperation::Cbrt, {-8}), -2));
  assert(close(executeNumeric(NumericOperation::Pow, {2, 3}), 8));
  assert(close(executeModulo(7.5, 2), 1.5));
  assert(close(executeModulo(-7.5, 2), -1.5));
  assert(close(executeNumeric(NumericOperation::Exp, {0}), 1));
  assert(close(executeNumeric(NumericOperation::Log, {std::exp(1.0)}), 1));
  assert(close(executeNumeric(NumericOperation::Log10, {1000}), 3));
  assert(close(executeNumeric(NumericOperation::Lerp, {10, 20, 0.25}), 12.5));
  assert(close(executeNumeric(NumericOperation::Sign, {-7.2}), -1));
  assert(close(executeNumeric(NumericOperation::Sign, {0}), 0));
  assert(close(executeNumeric(NumericOperation::Sign, {7.2}), 1));
  assert(close(executeNumeric(NumericOperation::Reciprocal, {4}), 0.25));
  assert(close(executeNumeric(NumericOperation::Square, {3}), 9));
  assert(close(executeNumeric(NumericOperation::Cube, {-2}), -8));
  assert(close(executeNumeric(NumericOperation::Max, {-345.345, -2.302}),
               -2.302));
  assert(close(executeNumeric(NumericOperation::Average, {3.432, -2.302}),
               0.565));
  assert(executeNumeric(NumericOperation::InRange, {0.7, 0, 1}) == 1.0);
  assert(executeNumeric(NumericOperation::InRange, {-0.1, 0, 1}) == 0.0);
  assert(executeNumeric(NumericOperation::InRange, {0, 0, 1}) == 1.0);
  assert(executeNumeric(NumericOperation::InRange, {1, 0, 1}) == 1.0);
  assert(executeNumeric(NumericOperation::IsFinite, {1.5}) == 1.0);
  assert(executeNumeric(NumericOperation::IsFinite,
                        {std::numeric_limits<double>::infinity()}) == 0.0);
  assert(executeNumeric(NumericOperation::IsFinite,
                        {std::numeric_limits<double>::quiet_NaN()}) == 0.0);
  assert(executeNumeric(NumericOperation::IsNan,
                        {std::numeric_limits<double>::quiet_NaN()}) == 1.0);
  assert(executeNumeric(NumericOperation::IsNan,
                        {std::numeric_limits<double>::infinity()}) == 0.0);

  assert(rejects([&] {
    (void)executeNumeric(NumericOperation::WeightedAverage, {1, 2, 1, -1});
  }));
  assert(rejects(
      [&] { (void)executeNumeric(NumericOperation::Clamp, {0, 1, -1}); }));
  assert(rejects([&] { (void)executeNumeric(NumericOperation::Sqrt, {-1}); }));
  assert(rejects([&] { (void)executeNumeric(NumericOperation::Log, {0}); }));
  assert(rejects([&] { (void)executeNumeric(NumericOperation::Log10, {-1}); }));
  assert(
      rejects([&] { (void)executeNumeric(NumericOperation::Pow, {-1, 0.5}); }));
  assert(rejects([&] { (void)executeNumeric(NumericOperation::Exp, {1000}); }));
  assert(rejects(
      [&] { (void)executeNumeric(NumericOperation::Reciprocal, {0}); }));
  assert(rejects([&] { (void)executeModulo(7.5, 0); }));
  assert(rejects([&] {
    (void)executeModulo(std::numeric_limits<double>::infinity(), 2);
  }));
  assert(rejects(
      [&] { (void)executeNumeric(NumericOperation::InRange, {0, 1, -1}); }));
  assert(rejects([&] {
    (void)executeNumeric(NumericOperation::Square,
                         {std::numeric_limits<double>::max()});
  }));
  assert(rejects([&] {
    (void)executeNumeric(NumericOperation::Abs,
                         {std::numeric_limits<double>::infinity()});
  }));

  auto invalidArity = numeric(NumericOperation::Abs, {1, 2});
  assert(rejects([&] { (void)verifyIrModule(std::move(invalidArity)); }));

  auto verifiedTensor = verifyIrModule(tensor(TensorOperation::Size, {1}));
  FelidaeKnowledgeRuntime noTensorBackend;
  assert(rejects([&] {
    (void)RegisterVm{}.executeMain(verifiedTensor, noTensorBackend);
  }));
  auto invalidTensorArity = tensor(TensorOperation::Size, {1, 2});
  assert(rejects([&] { (void)verifyIrModule(std::move(invalidTensorArity)); }));
  auto invalidTensorOperation = tensor(TensorOperation::Count, {1});
  assert(rejects(
      [&] { (void)verifyIrModule(std::move(invalidTensorOperation)); }));

  auto invalidBuiltinOperation = builtin(
      static_cast<BuiltinId>(static_cast<std::uint16_t>(BuiltinId::Last) + 1),
      "{}");
  assert(rejects(
      [&] { (void)verifyIrModule(std::move(invalidBuiltinOperation)); }));
  auto invalidBuiltinArity = builtin(BuiltinId::JsonParse, "{}");
  auto &invalidArityWords = invalidBuiltinArity.procedures.at(1).ir.words;
  invalidArityWords[6] = 0;
  invalidArityWords.erase(invalidArityWords.begin() + 7);
  assert(
      rejects([&] { (void)verifyIrModule(std::move(invalidBuiltinArity)); }));
  auto uninitializedBuiltinOperand = builtin(BuiltinId::JsonParse, "{}");
  auto &uninitializedWords =
      uninitializedBuiltinOperand.procedures.at(1).ir.words;
  uninitializedWords.erase(uninitializedWords.begin(),
                           uninitializedWords.begin() + 3);
  assert(rejects(
      [&] { (void)verifyIrModule(std::move(uninitializedBuiltinOperand)); }));

  auto invalid = returning(encodeIrNumber(1.0), IrConstantKind::Number);
  invalid.procedures.at(1).ir.words[1] = 99;
  assert(rejects([&] { (void)verifyIrModule(std::move(invalid)); }));

  IrModule loop;
  loop.sentencePieceModelIdentity = "sha256:test";
  loop.symbolTable = {{1}};
  loop.entryProcedure = 1;
  loop.ir.registerCount = 1;
  loop.ir.symbols = {1};
  loop.ir.words = {static_cast<IrWord>(IrOpcode::Call),
                   0,
                   0,
                   0,
                   static_cast<IrWord>(IrOpcode::Return),
                   0,
                   0,
                   static_cast<IrWord>(IrOpcode::End)};
  IrProcedure loopProcedure;
  loopProcedure.ir.registerCount = 0;
  loopProcedure.ir.words = {static_cast<IrWord>(IrOpcode::Jump), 0,
                            static_cast<IrWord>(IrOpcode::End)};
  loop.procedures.emplace(1, std::move(loopProcedure));
  auto verifiedLoop = verifyIrModule(std::move(loop));
  FelidaeKnowledgeRuntime loopRuntime;
  assert(rejects(
      [&] { (void)RegisterVm{16}.executeMain(verifiedLoop, loopRuntime); }));
  return 0;
}
