#include "CompilerFrontend.h"
#include "FelidaeIr.h"
#include "IntegerParser.h"
#include "IntegerTokenList.h"
#include "IrCodeGenerator.h"
#include "MixfixStateModel.h"
#include "SentencePieceModel.h"
#include "Symbol.h"
#include "form/BinaryIr.h"
#include "form/SemanticOperation.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

#include <sentencepiece_processor.h>

namespace {

Felidae::VmValue executeIrDirect(const Felidae::FelidaeIr &program,
                                 Felidae::VmRuntime &runtime) {
  constexpr Felidae::IrSymbolRef kTestEntry = 1;
  Felidae::IrModule module;
  module.entryProcedure = kTestEntry;
  module.ir.registerCount = 1;
  module.ir.symbols = {kTestEntry};
  module.ir.words = {
      static_cast<Felidae::IrWord>(Felidae::IrOpcode::Call),
      0,
      0,
      0,
      static_cast<Felidae::IrWord>(Felidae::IrOpcode::Return),
      0,
      0,
      static_cast<Felidae::IrWord>(Felidae::IrOpcode::End),
  };
  module.procedures.emplace(kTestEntry,
                            Felidae::IrProcedure{program, {}, {}, {}});
  auto largestSymbol = kTestEntry;
  for (const auto symbol : program.symbols)
    largestSymbol = std::max(largestSymbol, symbol);
  module.sentencePieceModelIdentity = "sha256:test";
  module.symbolTable.reserve(largestSymbol);
  for (Felidae::IrSymbolRef symbol = 1; symbol <= largestSymbol; ++symbol) {
    module.symbolTable.push_back({symbol});
  }
  return Felidae::RegisterVm{}.executeMain(
      Felidae::verifyIrModule(std::move(module)), runtime);
}

Felidae::VmValue executeModuleDirect(const Felidae::IrModule &module,
                                     Felidae::VmRuntime &runtime) {
  return Felidae::RegisterVm{}.executeMain(
      Felidae::verifyIrModule(Felidae::IrModule(module)), runtime);
}

bool containsOpcode(const Felidae::FelidaeIr &ir, Felidae::IrOpcode wanted) {
  for (std::size_t pc = 0; pc < ir.words.size();
       pc += Felidae::irInstructionWidth(ir, pc)) {
    if (static_cast<Felidae::IrOpcode>(ir.words[pc]) == wanted)
      return true;
  }
  return false;
}

bool containsOpcode(const Felidae::IrModule &module, Felidae::IrOpcode wanted) {
  if (containsOpcode(module.ir, wanted))
    return true;
  return std::any_of(module.procedures.begin(), module.procedures.end(),
                     [&](const auto &procedure) {
                       return containsOpcode(procedure.second.ir, wanted);
                     });
}

} // namespace

int main() {
  const std::filesystem::path testOutputDirectory(FELIDAE_TEST_OUTPUT_DIR);
  std::filesystem::create_directories(testOutputDirectory);
  const auto removeTemporary = [](const std::filesystem::path &path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  };
  // UTF-8 BOM bytes must never reach SentencePiece as a fake token before
  // the first source statement.
  const auto bomSource = testOutputDirectory / "utf8-bom-first-line.fx";
  {
    std::ofstream out(bomSource, std::ios::binary | std::ios::trunc);
    out.write("\xEF\xBB\xBF", 3);
    out << "main() => return 42\n";
  }
  assert(Felidae::readSourceFile(bomSource) == "main() => return 42\n");
  const auto bomModule = Felidae::compileProgramFileToIr(bomSource);
  assert(!bomModule.ir.words.empty());
  removeTemporary(bomSource);
  Felidae::VmDisplayContext display;
  display.textDecoder = [](std::span<const Felidae::PieceId> pieces) {
    std::vector<int> ids(pieces.begin(), pieces.end());
    std::string text;
    assert(Felidae::felidaeSentencePieceModel().Decode(ids, &text).ok());
    return text;
  };
  display.symbolDecoder = [](Felidae::IrSymbolRef symbol) {
    return Felidae::symbolNameForId(symbol);
  };
  const auto displayModuleValue = [&](const Felidae::IrModule &module,
                                      const Felidae::VmValue &value) {
    const auto verified = Felidae::verifyIrModule(Felidae::IrModule(module));
    return Felidae::vmValueToDisplayString(
        value, Felidae::makeIrDisplayContext(verified, display.textDecoder));
  };
  const auto executeModule = [](const Felidae::IrModule &module) {
    Felidae::FelidaeKnowledgeRuntime runtime;
    Felidae::RegisterVm vm;
    return vm.executeMain(Felidae::verifyIrModule(Felidae::IrModule(module)),
                          runtime);
  };
  const auto numericModule =
      Felidae::compileProgramFileToIr(FELIDAE_NUMERIC_OPERATIONS_FIXTURE_PATH);
  const auto numericResult =
      std::get<Felidae::VmMapPtr>(executeModule(numericModule));
  const std::array<double, 26> expectedNumeric{
      0.3, 0.8, 4.5, 3.2, 15.0, 17.5, 1.0,  4.0,  5.0, 5.0, 4.0, 3.0, 2.0,
      8.0, 1.0, 0.0, 3.0, 1.5,  12.5, -1.0, 0.25, 9.0, 8.0, 1.0, 1.0, 0.0};
  assert(numericResult &&
         numericResult->entries.size() == expectedNumeric.size());
  for (std::size_t index = 0; index < expectedNumeric.size(); ++index) {
    const auto actual = std::get<double>(numericResult->entries[index].second);
    assert(std::abs(actual - expectedNumeric[index]) <=
           1e-12 * std::max(1.0, std::abs(expectedNumeric[index])));
  }
  const auto tensorModule =
      Felidae::compileProgramFileToIr(FELIDAE_TENSOR_OPERATIONS_FIXTURE_PATH);
  assert(containsOpcode(tensorModule, Felidae::IrOpcode::Tensor));
  static_assert(std::variant_size_v<Felidae::VmValue> == 10,
                "production VM values must remain typed and AST-free");
  // This covers the production frontend: source -> SentencePiece IDs ->
  // integer parser -> AST compiler -> verified IR. No AST runtime exists.
  const auto program = Felidae::parseProgramFile(FELIDAE_PIPELINE_FIXTURE_PATH);
  assert(program.clauses.size() == 1);
  assert(program.clauses.front()->head.name == "main");
  // Source spans come from the original SentencePiece offsets. This covers
  // the parser's indexed span path: no secondary source tokenization and no
  // per-node linear rescan of the encoded stream.
  assert(program.clauses.front()->sourceSpan.startLine == 1);
  assert(program.clauses.front()->sourceSpan.startColumn == 1);
  assert(program.clauses.front()->sourceSpan.endLine >= 3);
  assert(program.clauses.front()->body.size() == 2);
  assert(program.clauses.front()->body.front()->sourceSpan.startLine == 2);
  assert(program.clauses.front()->body.back()->sourceSpan.startLine == 3);

  // Mixfix declarations and uses share one parser-owned registry over the
  // same line-wise SentencePiece stream. This must parse even when the
  // strict module compiler can lower a uniquely typed implementation into
  // the same Call IR used by ordinary function syntax.
  const auto mixfixProgram = Felidae::parseProgramText(
      "@mixfix(pattern: \"{left: number} combine {right: number}\")\n"
      "combineValue() => return left + right\n"
      "main() => return 2 combine 3\n");
  assert(mixfixProgram.clauses.size() == 2);
  assert(mixfixProgram.clauses.back()->head.name == "main");

  const auto directMixfixModule = Felidae::compileProgramTextToIr(
      "@mixfix(pattern: \"{left: number} combine {right: number}\")\n"
      "combineValue(left: number, right: number) => return left + right\n"
      "main() => return 17 combine 25\n");
  assert(std::get<double>(executeModule(directMixfixModule)) == 42.0);

  const auto thenPipelineModule = Felidae::compileProgramTextToIr(
      "increment(value: number) => return value + 1\n"
      "double(value: number) => return value * 2\n"
      "main() =>\n"
      "    return increment(value: 9)\n"
      "        then double(value: system.result)\n"
      "        then increment(value: system.result)\n");
  assert(std::get<double>(executeModule(thenPipelineModule)) == 21.0);

  const auto nestedThenPipelineModule = Felidae::compileProgramTextToIr(
      "increment(value: number) => return value + 1\n"
      "double(value: number) => return value * 2\n"
      "main() => return 10 then increment(value: system.result then "
      "double(value: system.result))\n");
  assert(std::get<double>(executeModule(nestedThenPipelineModule)) == 21.0);

  const auto thenAnchorMixfixModule = Felidae::compileProgramTextToIr(
      "@mixfix(pattern: \"join {left: number} then {right: number}\")\n"
      "joinThen(left: number, right: number) => return left + right\n"
      "main() => return join 20 then 22\n");
  assert(std::get<double>(executeModule(thenAnchorMixfixModule)) == 42.0);

  const auto nestedConditionalModule =
      Felidae::compileProgramTextToIr("classify(value: number) =>\n"
                                      "    if value > 10 then\n"
                                      "        return 2\n"
                                      "    else\n"
                                      "        if value > 0 then\n"
                                      "            return 1\n"
                                      "        else\n"
                                      "            return 0\n"
                                      "main() => return classify(value: 5)\n");
  assert(std::get<double>(executeModule(nestedConditionalModule)) == 1.0);

  const auto optionalEndModule = Felidae::compileProgramTextToIr(
      "classify(value: number) =>\n"
      "    if value > 10 then\n"
      "        return 2.0\n"
      "    else\n"
      "        if value > 0 then\n"
      "            return 1.0\n"
      "        else\n"
      "            return 0.0\n"
      "        end\n"
      "    end\n"
      "end\n"
      "main() => return classify(value: -1)\n"
      "end\n");
  assert(std::get<double>(executeModule(optionalEndModule)) == 0.0);

  const auto contextualEndMixfixModule = Felidae::compileProgramTextToIr(
      "@mixfix(pattern: \"wrap {value: number} end\")\n"
      "wrapValue(value: number) => return value\n"
      "end\n"
      "main() => return wrap 42 end\n"
      "end\n");
  assert(std::get<double>(executeModule(contextualEndMixfixModule)) == 42.0);

  const auto whereGuardModule = Felidae::compileProgramTextToIr(
      "eligible(score: number, active: number) =>\n"
      "    adjusted := score + 0\n"
      "    where adjusted >= 70\n"
      "    observed := active\n"
      "    where observed == 1.0\n"
      "    result := adjusted\n"
      "    return result\n"
      "else\n"
      "    return 0.0\n"
      "main() => return (pass: eligible(score: 82, active: 1.0), "
      "low: eligible(score: 40, active: 1.0), "
      "inactive: eligible(score: 90, active: 0.0))\n");
  const auto whereGuardResult = displayModuleValue(
      whereGuardModule, executeModule(whereGuardModule));
  assert(whereGuardResult == "{pass: 82.0, low: 0.0, inactive: 0.0}");

  const auto factExpressionModule =
      Felidae::compileProgramFileToIr(FELIDAE_FACT_EXPRESSION_FIXTURE_PATH);
  const auto factExpression = displayModuleValue(
      factExpressionModule, executeModule(factExpressionModule));
  assert(factExpression.find("crisp: 1.0") != std::string::npos);
  assert(factExpression.find("chance: 0.2") != std::string::npos);
  assert(factExpression.find("UnsupportedMode") != std::string::npos);
  assert(factExpression.find("category: companion") != std::string::npos);
  assert(factExpression.find("field: legs") != std::string::npos);

  // Hierarchy and temporal source intrinsics must survive the complete
  // deterministic frontend -> verified executable IR -> register VM path.
  // These are compiler tests, not isolated opcode-construction tests.
  const auto hierarchyModule = Felidae::compileProgramTextToIr(
      "Root(name: \"root\")\n"
      "Child extend Root(name: \"child\")\n"
      "main() => return isA(left: Child(name: \"child\"), right: Root(name: "
      "\"root\"))\n");
  assert(std::get<double>(executeModule(hierarchyModule)) == 1.0);

  const auto queriedHierarchyModule = Felidae::compileProgramTextToIr(
      "Animal(name: \"root\")\n"
      "Dog extend Animal(name: \"dog\")\n"
      "keep(fact: any) => return fact\n"
      "main() => return for_each_fact(Animal, keep)\n");
  const auto queriedFacts =
      std::get<Felidae::VmArrayPtr>(executeModule(queriedHierarchyModule));
  assert(queriedFacts && queriedFacts->values.size() == 2);

  const auto temporalModule = Felidae::compileProgramTextToIr(
      "Event(name: \"first\", effective_at: 10, priority: 1)\n"
      "Event(name: \"second\", effective_at: 20, priority: 2)\n"
      "main() => return temporalRank(effectiveAt: effective_at, priority: "
      "priority)\n");
  const auto temporalResult =
      std::get<Felidae::VmArrayPtr>(executeModule(temporalModule));
  assert(temporalResult && temporalResult->values.size() == 2);

  // Capability examples are production pipeline regressions, not parser-only
  // samples. Their ordered alternative proofs must reach safe actions through
  // verified IR, with numeric truth values rendered as 0.0/1.0 rather than
  // bool.
  const auto coffeeReasoningModule =
      Felidae::compileProgramFileToIr(FELIDAE_COFFEE_REASONING_FIXTURE_PATH);
  const auto coffeeReasoning = displayModuleValue(
      coffeeReasoningModule, executeModule(coffeeReasoningModule));
  assert(coffeeReasoning.find("dispense_coffee") != std::string::npos);
  assert(coffeeReasoning.find("refund") != std::string::npos);
  assert(coffeeReasoning.find("true") == std::string::npos);
  assert(coffeeReasoning.find("false") == std::string::npos);

  const auto hvacReasoningModule =
      Felidae::compileProgramFileToIr(FELIDAE_HVAC_REASONING_FIXTURE_PATH);
  const auto hvacReasoning = displayModuleValue(
      hvacReasoningModule, executeModule(hvacReasoningModule));
  assert(hvacReasoning.find("cool") != std::string::npos);
  assert(hvacReasoning.find("ventilate") != std::string::npos);
  assert(hvacReasoning.find("fault_lockout") != std::string::npos);
  assert(hvacReasoning.find("true") == std::string::npos);
  assert(hvacReasoning.find("false") == std::string::npos);

  // An unresolved overload must cross the compiler-model boundary exactly
  // once. The backend can select only parser-owned vocabulary entries; the
  // resulting executable IR is verified, serialized as FELBIR, loaded
  // through the binary verifier, and then executed directly by the VM.
  // This bounded backend is deliberately not a trained-model
  // quality test; it proves that the production routing cannot silently
  // bypass the SSM when overload resolution is genuinely ambiguous.
  class SelectingCompilerModel final : public Felidae::MixfixStateModel {
  public:
    std::size_t calls = 0;

    std::vector<Felidae::MixfixVocabularyId>
    transform(std::span<const Felidae::SentencePieceId> input,
              const Felidae::MixfixContext &context) override {
      assert(!input.empty());
      ++calls;
      const auto token = [&](Felidae::MixfixIrTokenKind kind,
                             Felidae::IrWord value = 0) {
        const auto found = std::find_if(
            context.outputVocabulary.begin(), context.outputVocabulary.end(),
            [&](const auto &candidate) {
              return candidate.kind == kind && candidate.value == value;
            });
        assert(found != context.outputVocabulary.end());
        return static_cast<Felidae::MixfixVocabularyId>(
            found - context.outputVocabulary.begin());
      };
      // The shell's first symbol is the captured variable and its next
      // symbol is the first matching overload. Load the capture, call
      // that bounded symbol reference, and return the selected value.
      return {
          token(Felidae::MixfixIrTokenKind::Accept),
          token(Felidae::MixfixIrTokenKind::Opcode,
                static_cast<Felidae::IrWord>(Felidae::IrOpcode::LoadSymbol)),
          token(Felidae::MixfixIrTokenKind::Register, 1),
          token(Felidae::MixfixIrTokenKind::SymbolReference, 0),
          token(Felidae::MixfixIrTokenKind::Opcode,
                static_cast<Felidae::IrWord>(Felidae::IrOpcode::Call)),
          token(Felidae::MixfixIrTokenKind::Register, 0),
          token(Felidae::MixfixIrTokenKind::SymbolReference, 1),
          token(Felidae::MixfixIrTokenKind::Register, 1),
          token(Felidae::MixfixIrTokenKind::Register, 1),
          token(Felidae::MixfixIrTokenKind::Opcode,
                static_cast<Felidae::IrWord>(Felidae::IrOpcode::Return)),
          token(Felidae::MixfixIrTokenKind::Register, 0),
          token(Felidae::MixfixIrTokenKind::Register, 0),
          token(Felidae::MixfixIrTokenKind::End),
      };
    }
  } selectingCompilerModel;
  Felidae::CompilerOptions selectingOptions;
  selectingOptions.mixfixModel = &selectingCompilerModel;

  const auto specificMixfixModule = Felidae::compileProgramTextToIr(
      "@overload(operator: chooseSpecific, pattern: \"choose {value}\", "
      "type: prefix, captures: {value: any}, result: number)\n"
      "chooseAny() => return 1\n"
      "@overload(operator: chooseSpecific, pattern: \"choose {value}\", "
      "type: prefix, captures: {value: number}, result: number)\n"
      "chooseNumber() => return 2\n"
      "main() => return choose 42\n",
      selectingOptions);
  assert(selectingCompilerModel.calls == 0);
  assert(std::get<double>(executeModule(specificMixfixModule)) == 2.0);

  bool incompatibleMixfixRejected = false;
  try {
    (void)Felidae::compileProgramTextToIr(
        "@overload(operator: chooseScalar, pattern: \"scalar {value}\", "
        "type: prefix, captures: {value: number}, result: number)\n"
        "chooseNumber() => return 1\n"
        "@overload(operator: chooseScalar, pattern: \"scalar {value}\", "
        "type: prefix, captures: {value: string}, result: number)\n"
        "chooseString() => return 2\n"
        "main() => return scalar [1, 2]\n",
        selectingOptions);
  } catch (const Felidae::IntegerParserError &) {
    incompatibleMixfixRejected = true;
  }
  assert(incompatibleMixfixRejected);
  assert(selectingCompilerModel.calls == 0);

  const auto selectedMixfixModule =
      Felidae::compileProgramTextToIr("@overload(\n"
                                      "    operator: chooseValue,\n"
                                      "    pattern: \"pick {value}\",\n"
                                      "    type: prefix,\n"
                                      "    captures: {value: number},\n"
                                      "    result: number,\n"
                                      "    precedence: prefix,\n"
                                      "    associativity: right,\n"
                                      "    cardinality: one,\n"
                                      "    effects: pure,\n"
                                      "    visibility: private\n"
                                      ")\n"
                                      "chooseFirst() => return value\n"
                                      "@overload(\n"
                                      "    operator: chooseValue,\n"
                                      "    pattern: \"pick {value}\",\n"
                                      "    type: prefix,\n"
                                      "    captures: {value: string},\n"
                                      "    result: number,\n"
                                      "    precedence: prefix,\n"
                                      "    associativity: right,\n"
                                      "    cardinality: one,\n"
                                      "    effects: pure,\n"
                                      "    visibility: private\n"
                                      ")\n"
                                      "chooseSecond() => return 99\n"
                                      "route(value: any) => return pick value\n"
                                      "main() => return route(value: 42)\n",
                                      selectingOptions);
  assert(selectingCompilerModel.calls == 1);
  const auto selectedMixfixPath =
      testOutputDirectory / "felidae_compiler_ssm_pipeline.bin";
  removeTemporary(selectedMixfixPath);
  Felidae::writeBinaryIr(
      selectedMixfixPath,
      Felidae::verifyIrModule(Felidae::IrModule(selectedMixfixModule)));
  const auto selectedMixfixBinary = Felidae::loadBinaryIr(
      selectedMixfixPath, Felidae::felidaeSentencePieceModelIdentity());
  Felidae::FelidaeKnowledgeRuntime selectedMixfixRuntime;
  assert(std::get<double>(Felidae::RegisterVm{}.executeMain(
             selectedMixfixBinary, selectedMixfixRuntime)) == 42.0);
  removeTemporary(selectedMixfixPath);

  class RejectingCompilerModel final : public Felidae::MixfixStateModel {
  public:
    std::size_t calls = 0;
    std::vector<Felidae::MixfixVocabularyId>
    transform(std::span<const Felidae::SentencePieceId> input,
              const Felidae::MixfixContext &context) override {
      assert(!input.empty());
      ++calls;
      const auto found = std::find_if(
          context.outputVocabulary.begin(), context.outputVocabulary.end(),
          [](const auto &token) {
            return token.kind == Felidae::MixfixIrTokenKind::Reject;
          });
      assert(found != context.outputVocabulary.end());
      return {static_cast<Felidae::MixfixVocabularyId>(
          found - context.outputVocabulary.begin())};
    }
  } rejectingCompilerModel;
  Felidae::CompilerOptions rejectingOptions;
  rejectingOptions.mixfixModel = &rejectingCompilerModel;
  bool trailingModelRejectionObserved = false;
  try {
    (void)Felidae::compileProgramTextToIr(
        "@mixfix(pattern: \"{value: number} selects\")\n"
        "selectNumber(value: number) => return value\n"
        "@mixfix(pattern: \"{value: string} selects\")\n"
        "selectText(value: string) => return 0\n"
        "route(value: any) => return value selects\n"
        "main() => return route(value: 42)\n",
        rejectingOptions);
  } catch (const Felidae::IntegerParserError &) {
    trailingModelRejectionObserved = true;
  }
  assert(trailingModelRejectionObserved);
  assert(rejectingCompilerModel.calls == 1);

  // Annotation-declared captures are compiler-local procedure parameters
  // when an implementation omits them from its callable head. The runtime
  // still sees only an ordinary verified Call with isolated bindings.
  const auto implicitMixfixModule = Felidae::compileProgramTextToIr(
      "@mixfix(pattern: \"{left: number} combine {right: number}\")\n"
      "combineValue() => return left + right\n"
      "main() => return 2 combine 3\n");
  assert(std::get<double>(executeModule(implicitMixfixModule)) == 5.0);

  // The parser preserves dotted names so qualified calls and `fx.` keys
  // remain one SentencePiece-aware spelling.  The compiler resolves only a
  // scoped `value.depth` spelling into GetField IR; Form never sees AST or
  // source syntax.
  const auto implicitFactMixfixModule = Felidae::compileProgramTextToIr(
      "@mixfix(pattern: \"seed {value: number}\")\n"
      "seedValue() => return Stage(depth: value)\n"
      "@mixfix(pattern: \"advance {value: mixfix} by {amount: number}\")\n"
      "advanceValue() => return Stage(depth: value.depth + amount)\n"
      "main() => return advance (seed 1) by 2\n");
  const auto implicitFactMixfix =
      std::get<Felidae::VmFactPtr>(executeModule(implicitFactMixfixModule));
  assert(implicitFactMixfix &&
         std::get<double>(implicitFactMixfix->fields.front().second) == 3.0);

  // `obj` is the established concise spelling of an unconstrained mixfix
  // capture. It must reuse Any matching, not introduce a second type path.
  const auto objectMixfixModule = Felidae::compileProgramTextToIr(
      "@mixfix(pattern: \"echo {value: obj}\")\n"
      "echoValue(value: expr) => return value\n"
      "main() => return echo 17\n");
  assert(std::get<double>(executeModule(objectMixfixModule)) == 17.0);

  // Nested leading and infix mixfix expressions recursively lower to Call
  // IR. The values prove capture boundaries are preserved at every depth.
  const auto nestedMixfixModule = Felidae::compileProgramTextToIr(
      "@mixfix(pattern: \"{left: number} blend {right: number}\")\n"
      "blend(left: number, right: number) => return left + right\n"
      "@mixfix(pattern: \"combine {left: number} with {right: number}\")\n"
      "combine(left: number, right: number) => return left * right\n"
      "@mixfix(pattern: \"scale {value: number} by {factor: number}\")\n"
      "scale(value: number, factor: number) => return value * factor\n"
      "@mixfix(pattern: \"offset {value: number} by {amount: number}\")\n"
      "offset(value: number, amount: number) => return value + amount\n"
      "main() =>\n"
      "leaf := 4 blend 5\n"
      "branch := combine (1 blend 2) with (3 blend leaf)\n"
      "deep := scale (combine branch with (offset (2 blend 3) by (1 blend 1))) "
      "by (2 blend 1)\n"
      "return (leaf: leaf, branch: branch, deep: deep)\n");
  const auto nestedMixfixResult =
      std::get<Felidae::VmMapPtr>(executeModule(nestedMixfixModule));
  assert(nestedMixfixResult && nestedMixfixResult->entries.size() == 3);
  assert(std::get<double>(nestedMixfixResult->entries[0].second) == 9.0);
  assert(std::get<double>(nestedMixfixResult->entries[1].second) == 36.0);
  assert(std::get<double>(nestedMixfixResult->entries[2].second) == 756.0);

  // `expr` is a structural mixfix capture: it accepts literals, facts,
  // variables, and nested mixfix values without weakening typed captures.
  const auto typedNestedMixfixModule = Felidae::compileProgramTextToIr(
      "@mixfix(pattern: \"reason {subject: expr} using {evidence: Evidence} "
      "within {context: Context}\")\n"
      "reasonFactValue() => return Explanation(subject: subject, evidence: "
      "evidence, context: context)\n"
      "@mixfix(pattern: \"validate {claim: expr} with {expected: string}\")\n"
      "validateReasonValue() => return Validation(claim: claim, expected: "
      "expected)\n"
      "main() =>\n"
      "evidence := Evidence(kind: \"observed\")\n"
      "context := Context(domain: \"animal-behaviour\")\n"
      "claim := reason \"tiger\" using evidence within context\n"
      "direct := validate (reason \"cat\" using evidence within context) with "
      "\"explanation\"\n"
      "return (bound: claim, direct: direct)\n");
  const auto typedNestedMixfixResult =
      std::get<Felidae::VmMapPtr>(executeModule(typedNestedMixfixModule));
  assert(typedNestedMixfixResult &&
         typedNestedMixfixResult->entries.size() == 2);
  assert(std::holds_alternative<Felidae::VmFactPtr>(
      typedNestedMixfixResult->entries[0].second));
  assert(std::holds_alternative<Felidae::VmFactPtr>(
      typedNestedMixfixResult->entries[1].second));

  // @overload uses the same annotation-capture binding path. Nested core
  // expressions remain deterministic and no parser detail reaches Form.
  const auto overloadModule = Felidae::compileProgramTextToIr(
      "@overload(operator: transformWith, pattern: \"{value} transform {model} "
      "with {count}\", "
      "type: mixfix, captures: {value: number, model: number, count: number}, "
      "result: number)\n"
      "transformValue() => return value + model * count\n"
      "main() =>\n"
      "result := 2 transform 3 with 4\n"
      "nested := (1 + 1) transform (2 + 1) with 2\n"
      "return (result: result, nested: nested)\n");
  const auto overloadResult =
      std::get<Felidae::VmMapPtr>(executeModule(overloadModule));
  assert(std::get<double>(overloadResult->entries[0].second) == 14.0);
  assert(std::get<double>(overloadResult->entries[1].second) == 8.0);

  // Capitalized named terms are primary fact values even without an
  // explicit schema declaration. They remain typed facts after a binary
  // round-trip rather than being mis-lowered as an unknown call.
  const auto adHocFactModule = Felidae::compileProgramTextToIr(
      "main() => return Plan(name: \"procedure\", enabled: true)\n");
  const auto adHocFact =
      std::get<Felidae::VmFactPtr>(executeModule(adHocFactModule));
  assert(adHocFact && adHocFact->fields.size() == 2);

  // The public execution path is parser -> verified IR -> register VM.
  // This test deliberately does not construct Interpreter or a legacy
  // runtime, so a passing pipeline cannot hide an AST execution fallback.
  const auto module =
      Felidae::compileProgramFileToIr(FELIDAE_PIPELINE_FIXTURE_PATH);
  // IrVerifier decodes instruction boundaries. Do not scan raw words for
  // opcode values: legal registers, constants, and symbols may share a
  // numeric value with an opcode.
  Felidae::IrVerifier::verify(module.ir);
  const auto verifiedModule =
      Felidae::verifyIrModule(Felidae::IrModule(module));
  Felidae::RegisterVm vm;
  Felidae::FelidaeKnowledgeRuntime moduleRuntime;
  const auto vmResult = vm.executeMain(verifiedModule, moduleRuntime);
  assert(Felidae::vmValueToDisplayString(
             vmResult, Felidae::makeIrDisplayContext(verifiedModule,
                                                     display.textDecoder)) ==
         "{answer: 42}");

  // The serialized artifact is executable in a fresh module/runtime path:
  // loader verification occurs before RegisterVm sees any instruction.
  const auto binaryPath =
      testOutputDirectory / "felidae_pipeline_roundtrip.bin";
  Felidae::writeBinaryIr(binaryPath, verifiedModule);
  const auto loadedModule = Felidae::loadBinaryIr(
      binaryPath, Felidae::felidaeSentencePieceModelIdentity());
  Felidae::FelidaeKnowledgeRuntime loadedRuntime;
  const auto textDecoder = [](std::span<const Felidae::PieceId> pieces) {
    std::vector<int> ids(pieces.begin(), pieces.end());
    std::string text;
    assert(Felidae::felidaeSentencePieceModel().Decode(ids, &text).ok());
    return text;
  };
  const auto binaryDisplay =
      Felidae::makeIrDisplayContext(loadedModule, textDecoder);
  assert(Felidae::vmValueToDisplayString(
             vm.executeMain(loadedModule, loadedRuntime), binaryDisplay) ==
         "{answer: 42}");
  const auto malformedPath = testOutputDirectory / "felidae_pipeline_bad.bin";
  {
    std::ofstream bad(malformedPath, std::ios::binary | std::ios::trunc);
    bad << "not a felidae IR";
  }
  bool malformedRejected = false;
  try {
    (void)Felidae::loadBinaryIr(malformedPath,
                                Felidae::felidaeSentencePieceModelIdentity());
  } catch (const Felidae::IrError &) {
    malformedRejected = true;
  }
  assert(malformedRejected);
  const auto expectRejectedCopy = [&](const char *suffix, const auto &mutate) {
    auto candidate = testOutputDirectory /
                     (std::string("felidae_pipeline_") + suffix + ".bin");
    std::filesystem::copy_file(
        binaryPath, candidate,
        std::filesystem::copy_options::overwrite_existing);
    mutate(candidate);
    bool rejected = false;
    try {
      (void)Felidae::loadBinaryIr(candidate,
                                  Felidae::felidaeSentencePieceModelIdentity());
    } catch (const Felidae::IrError &) {
      rejected = true;
    }
    removeTemporary(candidate);
    assert(rejected);
  };
  expectRejectedCopy("bad_version", [](const auto &candidate) {
    std::fstream file(candidate,
                      std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(8);
    const std::array<char, 4> version{2, 0, 0, 0};
    file.write(version.data(), version.size());
  });
  expectRejectedCopy("bad_endian", [](const auto &candidate) {
    std::fstream file(candidate,
                      std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(12);
    const std::array<char, 4> endian{};
    file.write(endian.data(), endian.size());
  });
  expectRejectedCopy("truncated", [](const auto &candidate) {
    std::filesystem::resize_file(candidate, 20);
  });
  removeTemporary(binaryPath);
  removeTemporary(malformedPath);

  // Primitive normal syntax is directly lowered from the integer parser to
  // canonical IR and evaluated by the register VM, without Interpreter.
  Felidae::IntegerTokenList expressionInput(
      Felidae::felidaeSentencePieceModel(), "6 * 7");
  Felidae::IntegerParser expressionParser(expressionInput);
  const auto expressionIr = expressionParser.compileExpressionIr();
  class NoRuntime final : public Felidae::VmRuntime {
  public:
    void installIrModule(const Felidae::IrModule &) override {}
    Felidae::IrSymbolRef
    resolveSymbol(Felidae::IrSymbolRef symbol) const override {
      return symbol;
    }
    Felidae::VmValue loadSymbol(Felidae::IrSymbolRef symbol) override {
      return symbols.at(symbol);
    }
    void storeSymbol(Felidae::IrSymbolRef symbol,
                     const Felidae::VmValue &value) override {
      symbols[symbol] = value;
    }
    Felidae::VmFactPtr
    retainFact(const Felidae::VmFactPtr &fact) override {
      return fact;
    }
    Felidae::VmFactPtr mutateFact(const Felidae::VmFactPtr &fact,
                                 Felidae::IrSymbolRef field,
                                 const Felidae::VmValue &value) override {
      if (!fact)
        throw Felidae::IrError("test mutation requires a fact");
      auto updated = std::make_shared<Felidae::VmFact>(*fact);
      const auto found =
          std::find_if(updated->fields.begin(), updated->fields.end(),
                       [&](const auto &entry) { return entry.first == field; });
      if (found == updated->fields.end())
        updated->fields.emplace_back(field, value);
      else
        found->second = value;
      return updated;
    }
    std::unordered_map<Felidae::IrSymbolRef, Felidae::VmValue> symbols;
  } noRuntime;
  Felidae::RegisterVm nativeVm;
  assert(std::get<double>(executeIrDirect(expressionIr, noRuntime)) == 42.0);
  Felidae::IntegerParser astExpressionParser(expressionInput);
  const auto astExpression = astExpressionParser.parseExpressionText();
  const auto astExpressionIr =
      Felidae::IrCodeGenerator::lowerExpression(astExpression);
  assert(std::get<double>(executeIrDirect(astExpressionIr, noRuntime)) == 42.0);
  const auto bindingProgram = Felidae::parseProgramText("answer := 6 * 7.");
  assert(bindingProgram.globals.size() == 1);
  const auto bindingIr = Felidae::IrCodeGenerator::lowerGlobalBinding(
      *bindingProgram.globals.front());
  assert(std::get<double>(executeIrDirect(bindingIr, noRuntime)) == 42.0);
  assert(std::get<double>(
             noRuntime.symbols.at(Felidae::symbolIdForName("answer"))) == 42.0);
  const auto entryProgram =
      Felidae::parseProgramText("main() => return (answer: 42).");
  assert(entryProgram.clauses.size() == 1);
  const auto entryIr = Felidae::IrCodeGenerator::lowerEntryMethod(
      *entryProgram.clauses.front());
  const auto entryResult =
      std::get<Felidae::VmMapPtr>(executeIrDirect(entryIr, noRuntime));
  assert(entryResult && entryResult->entries.size() == 1);
  assert(std::get<double>(entryResult->entries.front().second) == 42.0);
  auto directFact =
      std::make_shared<Felidae::MapExpr>(std::vector<Felidae::MapEntry>{
          Felidae::MapEntry{"age", std::make_shared<Felidae::NumberExpr>(4.0)},
      });
  directFact->factType = "Cat";
  const auto directFactIr =
      Felidae::IrCodeGenerator::lowerExpression(directFact);
  const auto directFactValue = executeIrDirect(directFactIr, noRuntime);
  const auto directFactResult = std::get<Felidae::VmFactPtr>(directFactValue);
  assert(directFactResult &&
         directFactResult->type == Felidae::symbolIdForName("Cat"));
  assert(directFactResult->fields.size() == 1 &&
         directFactResult->fields.front().first ==
             Felidae::symbolIdForName("age"));
  assert(std::get<double>(directFactResult->fields.front().second) == 4.0);
  const auto directModule =
      Felidae::compileProgramTextToIr("main() => return (answer: 42).");
  Felidae::IrVerifier::verify(directModule.ir);
  Felidae::FelidaeKnowledgeRuntime directRuntime;
  const auto directResult = executeModuleDirect(directModule, directRuntime);
  const auto directMap = std::get<Felidae::VmMapPtr>(directResult);
  assert(directMap &&
         std::get<double>(directMap->entries.front().second) == 42.0);
  assert(displayModuleValue(directModule, directResult) == "{answer: 42}");

  // Logical operands are control flow, not eager arithmetic. These calls
  // would divide by zero if the right operand were emitted before its
  // guarding branch.
  const auto shortCircuitModule = Felidae::compileProgramTextToIr(
      "explode(divisor: number) => return 1 / divisor.\n"
      "main() => return (both: false and explode(divisor: 0), "
      "either: true or explode(divisor: 0)).\n");
  Felidae::FelidaeKnowledgeRuntime shortCircuitRuntime;
  const auto shortCircuitResult =
      executeModuleDirect(shortCircuitModule, shortCircuitRuntime);
  assert(displayModuleValue(shortCircuitModule, shortCircuitResult) ==
         "{both: 0.0, either: 1.0}");

  // Repeated fact predicates are rows of one type, not duplicate procedure
  // declarations. They enter the runtime through the initializer's typed
  // MakeFact operations and QueryFacts invokes a deterministic callback.
  const auto repeatedFactsModule = Felidae::compileProgramTextToIr(
      "Color(value: \"red\").\n"
      "Color(value: \"blue\").\n"
      "colorValue(color: Color) => return color.value.\n"
      "main() => return for_each_fact(Color, colorValue).\n");
  assert(repeatedFactsModule.factTypes.size() == 1);
  Felidae::FelidaeKnowledgeRuntime repeatedFactsRuntime;
  const auto repeatedFactsResult =
      executeModuleDirect(repeatedFactsModule, repeatedFactsRuntime);
  assert(displayModuleValue(repeatedFactsModule, repeatedFactsResult) ==
         "[red, blue]");

  const auto concreteFactQueryModule = Felidae::compileProgramTextToIr(
      "School(name: \"North\", city: \"BLR\", active: 1.0)\n"
      "School(name: \"West\", city: \"MYS\", active: 0.0)\n"
      "School(name: \"Lake\", city: \"BLR\", active: 1.0)\n"
      "main() =>\n"
      "  selected := School.where(city: \"BLR\", active: 1.0)\n"
      "  return (all: School.all(), selected: selected, "
      "all_count: School.count(), selected_count: count(data: selected))\n"
      "end\n");
  Felidae::FelidaeKnowledgeRuntime concreteFactQueryRuntime;
  const auto concreteFactQueryResult = displayModuleValue(
      concreteFactQueryModule,
      executeModuleDirect(concreteFactQueryModule, concreteFactQueryRuntime));
  assert(concreteFactQueryResult.find("all_count: 3.0") != std::string::npos);
  assert(concreteFactQueryResult.find("selected_count: 2.0") !=
         std::string::npos);
  assert(concreteFactQueryResult.find("North") != std::string::npos);
  assert(concreteFactQueryResult.find("Lake") != std::string::npos);
  assert(concreteFactQueryResult.find("West") != std::string::npos);

  const auto factDmlModule = Felidae::compileProgramTextToIr(
      "School(id: 1, name: \"North\", city: \"BLR\", students: 420, active: 1.0)\n"
      "School(id: 2, name: \"West\", city: \"MYS\", students: 280, active: 0.0)\n"
      "School(id: 3, name: \"Lake\", city: \"BLR\", students: 350, active: 1.0)\n"
      "Teacher(name: \"Ada\", school_id: 1)\n"
      "Teacher(name: \"Grace\", school_id: 9)\n"
      "main() =>\n"
      "  limited := School.where(active: 1.0).AndWhere(city: \"BLR\").limit(records: 1)\n"
      "  alternatives := School.where(city: \"MYS\").OrWhere(name: \"Lake\")\n"
      "  projected := School.select(fields: [\"name\", \"city\"], match: {active: 1.0})\n"
      "  joined := School.leftJoin(type: Teacher, left: \"id\", right: \"school_id\")\n"
      "  inserted := School.insert(values: {id: 4, name: \"Hill\", city: \"BLR\", students: 100, active: 1.0})\n"
      "  updated := School.update(match: {id: 2}, values: {active: 1.0})\n"
      "  deleted := School.delete(match: {id: 3})\n"
      "  return (limited: count(data: limited), alternatives: count(data: alternatives), "
      "projected: projected, joined: count(data: joined), inserted: inserted, "
      "updated: count(data: updated), deleted: deleted, remaining: School.count(), "
      "total: School.sum(field: \"students\"), average: School.average(field: \"students\"))\n"
      "end\n");
  Felidae::FelidaeKnowledgeRuntime factDmlRuntime;
  const auto factDmlResult = displayModuleValue(
      factDmlModule, executeModuleDirect(factDmlModule, factDmlRuntime));
  assert(factDmlResult.find("limited: 1.0") != std::string::npos);
  assert(factDmlResult.find("alternatives: 2.0") != std::string::npos);
  assert(factDmlResult.find("joined: 3.0") != std::string::npos);
  assert(factDmlResult.find("updated: 1.0") != std::string::npos);
  assert(factDmlResult.find("deleted: 1.0") != std::string::npos);
  assert(factDmlResult.find("remaining: 3.0") != std::string::npos);
  assert(factDmlResult.find("total: 800.0") != std::string::npos);

  const auto repeatedFieldsModule = Felidae::compileProgramTextToIr(
      "Color(value: \"red\", tag: \"warm\", tag: \"primary\").\n"
      "Color(value: \"blue\", tag: \"cool\", tag: \"primary\").\n"
      "colorTags(color: Color) => return color.tag.\n"
      "main() => return for_each_fact(Color, colorTags).\n");
  Felidae::FelidaeKnowledgeRuntime repeatedFieldsRuntime;
  assert(displayModuleValue(repeatedFieldsModule,
                            executeModuleDirect(repeatedFieldsModule,
                                                repeatedFieldsRuntime)) ==
         "[[warm, primary], [cool, primary]]");

  // Real source semantic intrinsic -> structured compiler IR
  // operation ID -> typed runtime-model result. No tokenizer or symbol ID
  // crosses the semantic execution boundary.
  const auto semanticModule = Felidae::compileProgramTextToIr(
      "main() => return semantic_identity(42).\n");
  const auto semanticIr =
      Felidae::verifyIrModule(Felidae::IrModule(semanticModule));
  assert(Felidae::containsRuntimeSemanticOperation(semanticIr));
  class IdentityRuntimeModel final : public Felidae::RuntimeStateModel {
  public:
    Felidae::VmValue evaluate(const Felidae::RuntimeOperation &operation,
                              std::span<const Felidae::VmValue> inputs,
                              Felidae::RuntimeContext &) override {
      assert(operation.id == static_cast<std::uint16_t>(
                                 Felidae::SemanticOperationId::Identity));
      assert(inputs.size() == 1);
      return inputs.front();
    }
  } identityModel;
  Felidae::FelidaeKnowledgeRuntime semanticRuntime(&identityModel);
  assert(std::get<double>(Felidae::RegisterVm{}.executeMain(
             semanticIr, semanticRuntime)) == 42.0);
  const auto executeBinaryModule = [&](const Felidae::IrModule &candidate) {
    const auto path =
        testOutputDirectory / "felidae_pipeline_feature_roundtrip.bin";
    try {
      Felidae::writeBinaryIr(
          path, Felidae::verifyIrModule(Felidae::IrModule(candidate)));
      const auto loaded = Felidae::loadBinaryIr(
          path, Felidae::felidaeSentencePieceModelIdentity());
      Felidae::FelidaeKnowledgeRuntime runtime;
      const auto result = nativeVm.executeMain(loaded, runtime);
      std::filesystem::remove(path);
      return Felidae::vmValueToDisplayString(
          result, Felidae::makeIrDisplayContext(loaded, display.textDecoder));
    } catch (...) {
      std::filesystem::remove(path);
      throw;
    }
  };
  assert(executeBinaryModule(directModule) == "{answer: 42}");
  const auto globalsModule = Felidae::compileProgramTextToIr(
      "answer := 6 * 7.\nmain() => return (answer: answer).");
  Felidae::IrVerifier::verify(globalsModule.ir);
  Felidae::FelidaeKnowledgeRuntime globalsRuntime;
  const auto globalsResult = executeModuleDirect(globalsModule, globalsRuntime);
  assert(displayModuleValue(globalsModule, globalsResult) == "{answer: 42}");
  assert(executeBinaryModule(globalsModule) == "{answer: 42}");
  const auto textGlobalsModule = Felidae::compileProgramTextToIr(
      "label := \"meow\".\nmain() => return (label: label).");
  Felidae::FelidaeKnowledgeRuntime textGlobalsRuntime;
  const auto textGlobalsResult =
      executeModuleDirect(textGlobalsModule, textGlobalsRuntime);
  assert(displayModuleValue(textGlobalsModule, textGlobalsResult) ==
         "{label: meow}");
  assert(executeBinaryModule(textGlobalsModule) == "{label: meow}");
  const auto conditionalModule = Felidae::compileProgramTextToIr(
      "main() =>\nif 3 > 2 then\nreturn (result: \"yes\")\nelse\nreturn "
      "(result: \"no\").");
  Felidae::IrVerifier::verify(conditionalModule.ir);
  Felidae::FelidaeKnowledgeRuntime conditionalRuntime;
  const auto conditionalResult =
      executeModuleDirect(conditionalModule, conditionalRuntime);
  assert(displayModuleValue(conditionalModule, conditionalResult) ==
         "{result: yes}");
  assert(executeBinaryModule(conditionalModule) == "{result: yes}");
  const auto falseConditionalModule = Felidae::compileProgramTextToIr(
      "main() =>\nif 2 > 3 then\nreturn (result: \"yes\")\nelse\nreturn "
      "(result: \"no\").");
  Felidae::FelidaeKnowledgeRuntime falseConditionalRuntime;
  const auto falseConditionalResult =
      executeModuleDirect(falseConditionalModule, falseConditionalRuntime);
  assert(displayModuleValue(falseConditionalModule, falseConditionalResult) ==
         "{result: no}");
  assert(executeBinaryModule(falseConditionalModule) == "{result: no}");
  const auto localBindingModule = Felidae::compileProgramTextToIr(
      "main() =>\nvalue := 40\nanswer := value + 2\nreturn (answer: answer).");
  Felidae::IrVerifier::verify(localBindingModule.ir);
  Felidae::FelidaeKnowledgeRuntime localBindingRuntime;
  const auto localBindingResult =
      executeModuleDirect(localBindingModule, localBindingRuntime);
  assert(displayModuleValue(localBindingModule, localBindingResult) ==
         "{answer: 42}");
  // Signatures are collected before lowering bodies, so main can call a
  // later declaration and that declaration can in turn call another later
  // declaration. Parameters live in independent VM call frames and named
  // arguments are mapped by their declaration-order symbols.
  const auto callsModule = Felidae::compileProgramTextToIr(
      "main() => return (answer: twice(value: 21)).\n"
      "twice(value: expr) =>\n"
      "doubled := add(right: value, left: value)\n"
      "return doubled.\n"
      "add(left: expr, right: expr) => return left + right.");
  Felidae::IrVerifier::verify(callsModule.ir);
  Felidae::FelidaeKnowledgeRuntime callsRuntime;
  const auto callsResult = executeModuleDirect(callsModule, callsRuntime);
  assert(displayModuleValue(callsModule, callsResult) == "{answer: 42}");
  assert(executeBinaryModule(callsModule) == "{answer: 42}");
  // A procedure receives an independent lexical frame.  Its local `seed`
  // cannot overwrite the caller's local of the same name.
  const auto isolatedScopeModule = Felidae::compileProgramTextToIr(
      "main() =>\n"
      "seed := 5\n"
      "return (caller: seed, callee: wrap(value: 7)).\n"
      "wrap(value: expr) =>\n"
      "seed := value + 1\n"
      "return seed.");
  Felidae::IrVerifier::verify(isolatedScopeModule.ir);
  Felidae::FelidaeKnowledgeRuntime isolatedScopeRuntime;
  assert(displayModuleValue(
             isolatedScopeModule,
             executeModuleDirect(isolatedScopeModule, isolatedScopeRuntime)) ==
         "{caller: 5, callee: 8}");
  assert(executeBinaryModule(isolatedScopeModule) == "{caller: 5, callee: 8}");
  const auto scopeCompileRejected = [](const std::string &source) {
    try {
      (void)Felidae::compileProgramTextToIr(source);
      return false;
    } catch (const Felidae::IntegerParserError &) {
      return true;
    }
  };
  assert(scopeCompileRejected(
      "value := 1.\nvalue := 2.\nmain() => return value."));
  assert(scopeCompileRejected("value := 1.\nmain() => return "
                              "value.\nhelper(value: expr) => return value."));
  assert(
      scopeCompileRejected("main() => return helper(value: 1).\nhelper(value: "
                           "expr) =>\nvalue := 2\nreturn value."));
  assert(scopeCompileRejected("main() => return system.result.\n"));
  assert(
      scopeCompileRejected("main() => return helper(value: 1).\nhelper(value: "
                           "expr, value: expr) => return value."));
  const auto recursionModule = Felidae::compileProgramTextToIr(
      "main() => return (answer: countdown(value: 4)).\n"
      "countdown(value: expr) =>\n"
      "if value == 0 then\n"
      "return 0\n"
      "else\n"
      "return countdown(value: value - 1).");
  Felidae::FelidaeKnowledgeRuntime recursionRuntime(nullptr, 1024, 16);
  const auto recursionResult =
      executeModuleDirect(recursionModule, recursionRuntime);
  assert(displayModuleValue(recursionModule, recursionResult) ==
         "{answer: 0.0}");
  assert(executeBinaryModule(recursionModule) == "{answer: 0.0}");
  bool undefinedProcedureRejected = false;
  try {
    (void)Felidae::compileProgramTextToIr(
        "main() => return (answer: helper()).");
  } catch (const Felidae::IntegerParserError &) {
    undefinedProcedureRejected = true;
  }
  assert(undefinedProcedureRejected);
  const auto runtimeExpressionIr =
      Felidae::tryCompileExpressionTextToIr("10 - 3");
  assert(runtimeExpressionIr);
  assert(std::get<double>(executeIrDirect(*runtimeExpressionIr, noRuntime)) ==
         7.0);
  const auto comparisonIr = Felidae::tryCompileExpressionTextToIr("3 < 7");
  assert(comparisonIr);
  assert(std::get<double>(executeIrDirect(*comparisonIr, noRuntime)) == 1.0);
  const auto logicIr =
      Felidae::tryCompileExpressionTextToIr("not false and true");
  assert(logicIr);
  assert(std::get<double>(executeIrDirect(*logicIr, noRuntime)) == 1.0);
  const auto moduloIr = Felidae::tryCompileExpressionTextToIr("17 % 5");
  assert(moduloIr);
  assert(std::get<double>(executeIrDirect(*moduloIr, noRuntime)) == 2.0);
  const auto callIr = Felidae::tryCompileExpressionTextToIr("combine(6, 7)");
  assert(callIr);
  Felidae::IrVerifier::verify(*callIr);
  const auto namedCallIr =
      Felidae::tryCompileExpressionTextToIr("combine(left: 6, right: 7)");
  assert(namedCallIr);
  Felidae::IrVerifier::verify(*namedCallIr);
  const auto textIr = Felidae::tryCompileExpressionTextToIr("\"mixfix text\"");
  assert(textIr);
  assert(Felidae::vmValueToDisplayString(executeIrDirect(*textIr, noRuntime),
                                         display) == "mixfix text");
  const auto nilIr = Felidae::tryCompileExpressionTextToIr("nil");
  assert(nilIr);
  assert(std::holds_alternative<Felidae::VmNil>(
      executeIrDirect(*nilIr, noRuntime)));
  const auto arrayIr = Felidae::tryCompileExpressionTextToIr("[1, 2, 3]");
  assert(arrayIr);
  const auto array =
      std::get<Felidae::VmArrayPtr>(executeIrDirect(*arrayIr, noRuntime));
  assert(array && array->values.size() == 3 &&
         std::get<double>(array->values[2]) == 3.0);
  const auto mapIr =
      Felidae::tryCompileExpressionTextToIr("{answer: 42, enabled: true}");
  assert(mapIr);
  const auto map =
      std::get<Felidae::VmMapPtr>(executeIrDirect(*mapIr, noRuntime));
  assert(map && map->entries.size() == 2);
  assert(std::get<double>(map->entries[0].second) == 42.0);
  assert(std::get<double>(map->entries[1].second) == 1.0);
  class SpanModel final : public Felidae::MixfixStateModel {
  public:
    std::vector<Felidae::MixfixVocabularyId>
    transform(std::span<const Felidae::SentencePieceId> input,
              const Felidae::MixfixContext &) override {
      assert(!input.empty());
      return {0, 1, 2, 3, 4, 2, 2, 5};
    }
  } spanModel;
  Felidae::MixfixContext mixfixContext;
  mixfixContext.constantReferences = {0};
  mixfixContext.outputVocabulary = {
      {Felidae::MixfixIrTokenKind::Accept, 0},
      {Felidae::MixfixIrTokenKind::Opcode,
       static_cast<Felidae::IrWord>(Felidae::IrOpcode::LoadConst)},
      {Felidae::MixfixIrTokenKind::Register, 0},
      {Felidae::MixfixIrTokenKind::ConstantReference, 0},
      {Felidae::MixfixIrTokenKind::Opcode,
       static_cast<Felidae::IrWord>(Felidae::IrOpcode::Return)},
      {Felidae::MixfixIrTokenKind::End, 0},
  };
  Felidae::FelidaeIr mixfixShell;
  mixfixShell.registerCount = 1;
  mixfixShell.constants = {
      {Felidae::IrConstantKind::Number, Felidae::encodeIrNumber(42.0)}};
  Felidae::IntegerTokenList mixfixInput(Felidae::felidaeSentencePieceModel(),
                                        "unresolved mixfix span");
  Felidae::IntegerParser mixfixParser(mixfixInput);
  const auto mixfixIr = mixfixParser.compileVerifiedMixfixSpanIr(
      spanModel, mixfixContext, mixfixShell, 0, mixfixInput.entries().size());
  assert(std::get<double>(executeIrDirect(mixfixIr, noRuntime)) == 42.0);
  const auto fieldIr =
      Felidae::tryCompileExpressionTextToIr("{answer: 42}:answer");
  assert(fieldIr);
  assert(std::get<double>(executeIrDirect(*fieldIr, noRuntime)) == 42.0);
  const auto aggregateEqualityIr = Felidae::tryCompileExpressionTextToIr(
      "[1, {answer: 42}] == [1, {answer: 42}]");
  assert(aggregateEqualityIr);
  assert(std::get<double>(executeIrDirect(*aggregateEqualityIr, noRuntime)) ==
         1.0);
}
