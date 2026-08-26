#include "CompilerFrontend.h"
#include "FelidaeIr.h"
#include "form/BinaryIr.h"
#include "form/SemanticOperation.h"
#include "IntegerParser.h"
#include "IntegerTokenList.h"
#include "MixfixStateModel.h"
#include "SentencePieceModel.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <variant>
#include <unordered_map>

#include <sentencepiece_processor.h>

namespace {

Felidae::VmValue executeIrDirect(const Felidae::FelidaeIr& program,
                                     Felidae::VmRuntime& runtime) {
    constexpr Felidae::IrSymbolRef kTestEntry = 0xf11da00000000001ull;
    Felidae::IrModule module;
    module.entryProcedure = kTestEntry;
    module.ir.registerCount = 1;
    module.ir.symbols = {kTestEntry};
    module.ir.words = {
        static_cast<Felidae::IrWord>(Felidae::IrOpcode::Call), 0, 0, 0,
        static_cast<Felidae::IrWord>(Felidae::IrOpcode::Return), 0, 0,
        static_cast<Felidae::IrWord>(Felidae::IrOpcode::End),
    };
    module.procedures.emplace(kTestEntry, Felidae::IrProcedure{program, {}, {}, {}});
    return Felidae::RegisterVm{}.executeMain(Felidae::verifyIrModule(std::move(module)), runtime);
}

Felidae::VmValue executeModuleDirect(const Felidae::IrModule& module,
                                         Felidae::VmRuntime& runtime) {
    return Felidae::RegisterVm{}.executeMain(Felidae::verifyIrModule(Felidae::IrModule(module)), runtime);
}

} // namespace

int main() {
    const std::filesystem::path testOutputDirectory(FELIDAE_TEST_OUTPUT_DIR);
    std::filesystem::create_directories(testOutputDirectory);
    const auto removeTemporary = [](const std::filesystem::path& path) {
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
    display.symbolDecoder = [&](Felidae::IrSymbolRef symbol) {
        const auto pieces = Felidae::runtimeSymbolPieces(symbol);
        return pieces.empty() ? std::string{} : display.textDecoder(pieces);
    };
    const auto executeModule=[](const Felidae::IrModule& module){
        Felidae::FelidaeKnowledgeRuntime runtime;
        Felidae::RegisterVm vm;
        return vm.executeMain(Felidae::verifyIrModule(Felidae::IrModule(module)),runtime);
    };
    static_assert(std::variant_size_v<Felidae::VmValue> == 8,
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

    const auto thenAnchorMixfixModule = Felidae::compileProgramTextToIr(
        "@mixfix(pattern: \"join {left: number} then {right: number}\")\n"
        "joinThen(left: number, right: number) => return left + right\n"
        "main() => return join 20 then 22\n");
    assert(std::get<double>(executeModule(thenAnchorMixfixModule)) == 42.0);

    // Hierarchy and temporal source intrinsics must survive the complete
    // deterministic frontend -> verified IR -> ISA lowering -> ISA VM path.
    // These are compiler tests, not assembler-only opcode tests.
    const auto hierarchyModule = Felidae::compileProgramTextToIr(
        "Root(name: \"root\")\n"
        "Child extend Root(name: \"child\")\n"
        "main() => return isA(left: Child(name: \"child\"), right: Root(name: \"root\"))\n");
    assert(std::get<double>(executeModule(hierarchyModule)) == 1.0);

    const auto designatedHierarchyModule = Felidae::compileProgramTextToIr(
        "Animal(name: \"root\") as animals\n"
        "Dog extend Animal(name: \"dog\")\n"
        "keep(fact: any) => return fact\n"
        "main() => return for_each_fact(animals, keep)\n");
    const auto designatedFacts = std::get<Felidae::VmArrayPtr>(
        executeModule(designatedHierarchyModule));
    assert(designatedFacts && designatedFacts->values.size() == 2);

    const auto temporalModule = Felidae::compileProgramTextToIr(
        "Event(name: \"first\", effective_at: 10, priority: 1)\n"
        "Event(name: \"second\", effective_at: 20, priority: 2)\n"
        "main() => return temporalRank(effectiveAt: effective_at, priority: priority)\n");
    const auto temporalResult = std::get<Felidae::VmArrayPtr>(executeModule(temporalModule));
    assert(temporalResult && temporalResult->values.size() == 2);

    // Capability examples are production pipeline regressions, not parser-only
    // samples. Their ordered alternative proofs must reach safe actions through
    // verified ISA, with numeric truth values rendered as 0/1 rather than bool.
    const auto coffeeReasoningModule = Felidae::compileProgramFileToIr(
        FELIDAE_COFFEE_REASONING_FIXTURE_PATH);
    const auto coffeeReasoning = Felidae::vmValueToDisplayString(
        executeModule(coffeeReasoningModule), display);
    assert(coffeeReasoning.find("dispense_coffee") != std::string::npos);
    assert(coffeeReasoning.find("refund") != std::string::npos);
    assert(coffeeReasoning.find("true") == std::string::npos);
    assert(coffeeReasoning.find("false") == std::string::npos);

    const auto hvacReasoningModule = Felidae::compileProgramFileToIr(
        FELIDAE_HVAC_REASONING_FIXTURE_PATH);
    const auto hvacReasoning = Felidae::vmValueToDisplayString(
        executeModule(hvacReasoningModule), display);
    assert(hvacReasoning.find("cool") != std::string::npos);
    assert(hvacReasoning.find("ventilate") != std::string::npos);
    assert(hvacReasoning.find("fault_lockout") != std::string::npos);
    assert(hvacReasoning.find("true") == std::string::npos);
    assert(hvacReasoning.find("false") == std::string::npos);

    // An unresolved overload must cross the compiler-model boundary exactly
    // once. The backend can select only parser-owned vocabulary entries; the
    // resulting compiler IR is verified, lowered to ISA, serialized as
    // FELBIN, loaded through the binary verifier, and then executed by the
    // ISA-only VM. This bounded backend is deliberately not a trained-model
    // quality test; it proves that the production routing cannot silently
    // bypass the SSM when overload resolution is genuinely ambiguous.
    class SelectingCompilerModel final : public Felidae::MixfixStateModel {
    public:
        std::size_t calls = 0;

        std::vector<Felidae::MixfixVocabularyId> transform(
            std::span<const Felidae::SentencePieceId> input,
            const Felidae::MixfixContext& context) override {
            assert(!input.empty());
            ++calls;
            const auto token = [&](Felidae::MixfixIrTokenKind kind,
                                   Felidae::IrWord value = 0) {
                const auto found = std::find_if(context.outputVocabulary.begin(),
                    context.outputVocabulary.end(), [&](const auto& candidate) {
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
    const auto selectedMixfixModule = Felidae::compileProgramTextToIr(
        "@overload(\n"
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
    const auto selectedMixfixPath = testOutputDirectory /
        "felidae_compiler_ssm_pipeline.bin";
    removeTemporary(selectedMixfixPath);
    Felidae::writeBinaryIr(selectedMixfixPath,
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
        std::vector<Felidae::MixfixVocabularyId> transform(
            std::span<const Felidae::SentencePieceId> input,
            const Felidae::MixfixContext& context) override {
            assert(!input.empty());
            ++calls;
            const auto found = std::find_if(context.outputVocabulary.begin(),
                context.outputVocabulary.end(), [](const auto& token) {
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
    } catch (const Felidae::IntegerParserError&) {
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
    const auto implicitFactMixfix = std::get<Felidae::VmFactPtr>(
        executeModule(implicitFactMixfixModule));
    assert(implicitFactMixfix && std::get<double>(implicitFactMixfix->fields.front().second) == 3.0);

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
        "deep := scale (combine branch with (offset (2 blend 3) by (1 blend 1))) by (2 blend 1)\n"
        "return (leaf: leaf, branch: branch, deep: deep)\n");
    const auto nestedMixfixResult = std::get<Felidae::VmMapPtr>(
        executeModule(nestedMixfixModule));
    assert(nestedMixfixResult && nestedMixfixResult->entries.size() == 3);
    assert(std::get<double>(nestedMixfixResult->entries[0].second) == 9.0);
    assert(std::get<double>(nestedMixfixResult->entries[1].second) == 36.0);
    assert(std::get<double>(nestedMixfixResult->entries[2].second) == 756.0);

    // `expr` is a structural mixfix capture: it accepts literals, facts,
    // variables, and nested mixfix values without weakening typed captures.
    const auto typedNestedMixfixModule = Felidae::compileProgramTextToIr(
        "@mixfix(pattern: \"reason {subject: expr} using {evidence: Evidence} within {context: Context}\")\n"
        "reasonFactValue() => return Explanation(subject: subject, evidence: evidence, context: context)\n"
        "@mixfix(pattern: \"validate {claim: expr} with {expected: string}\")\n"
        "validateReasonValue() => return Validation(claim: claim, expected: expected)\n"
        "main() =>\n"
        "evidence := Evidence(kind: \"observed\")\n"
        "context := Context(domain: \"animal-behaviour\")\n"
        "claim := reason \"tiger\" using evidence within context\n"
        "direct := validate (reason \"cat\" using evidence within context) with \"explanation\"\n"
        "return (bound: claim, direct: direct)\n");
    const auto typedNestedMixfixResult = std::get<Felidae::VmMapPtr>(
        executeModule(typedNestedMixfixModule));
    assert(typedNestedMixfixResult && typedNestedMixfixResult->entries.size() == 2);
    assert(std::holds_alternative<Felidae::VmFactPtr>(typedNestedMixfixResult->entries[0].second));
    assert(std::holds_alternative<Felidae::VmFactPtr>(typedNestedMixfixResult->entries[1].second));

    // @overload uses the same annotation-capture binding path. Nested core
    // expressions remain deterministic and no parser detail reaches Form.
    const auto overloadModule = Felidae::compileProgramTextToIr(
        "@overload(operator: transformWith, pattern: \"{value} transform {model} with {count}\", "
        "type: mixfix, captures: {value: number, model: number, count: number}, result: number)\n"
        "transformValue() => return value + model * count\n"
        "main() =>\n"
        "result := 2 transform 3 with 4\n"
        "nested := (1 + 1) transform (2 + 1) with 2\n"
        "return (result: result, nested: nested)\n");
    const auto overloadResult = std::get<Felidae::VmMapPtr>(
        executeModule(overloadModule));
    assert(std::get<double>(overloadResult->entries[0].second) == 14.0);
    assert(std::get<double>(overloadResult->entries[1].second) == 8.0);

    // Capitalized named terms are primary fact values even without an
    // explicit schema declaration. They remain typed facts after a binary
    // round-trip rather than being mis-lowered as an unknown call.
    const auto adHocFactModule = Felidae::compileProgramTextToIr(
        "main() => return Plan(name: \"procedure\", enabled: true)\n");
    const auto adHocFact = std::get<Felidae::VmFactPtr>(
        executeModule(adHocFactModule));
    assert(adHocFact && adHocFact->fields.size() == 2);

    // The public execution path is parser -> verified IR -> register VM.
    // This test deliberately does not construct Interpreter or a legacy
    // runtime, so a passing pipeline cannot hide an AST execution fallback.
    const auto module = Felidae::compileProgramFileToIr(FELIDAE_PIPELINE_FIXTURE_PATH);
    // IrVerifier decodes instruction boundaries. Do not scan raw words for
    // opcode values: legal registers, constants, and symbols may share a
    // numeric value with an opcode.
    Felidae::IrVerifier::verify(module.ir);
    const auto verifiedModule=Felidae::verifyIrModule(Felidae::IrModule(module));
    Felidae::RegisterVm vm;
    Felidae::FelidaeKnowledgeRuntime moduleRuntime;
    const auto vmResult = vm.executeMain(verifiedModule, moduleRuntime);
    assert(Felidae::vmValueToDisplayString(vmResult, display) == "{answer: 42}");

    // The serialized artifact is executable in a fresh module/runtime path:
    // loader verification occurs before RegisterVm sees any instruction.
    const auto binaryPath = testOutputDirectory / "felidae_pipeline_roundtrip.bin";
    Felidae::writeBinaryIr(binaryPath, verifiedModule);
    const auto loadedModule = Felidae::loadBinaryIr(binaryPath, Felidae::felidaeSentencePieceModelIdentity());
    Felidae::FelidaeKnowledgeRuntime loadedRuntime;
    const auto textDecoder = [](std::span<const Felidae::PieceId> pieces) {
        std::vector<int> ids(pieces.begin(), pieces.end());
        std::string text;
        assert(Felidae::felidaeSentencePieceModel().Decode(ids, &text).ok());
        return text;
    };
    const auto binaryDisplay = Felidae::makeIrDisplayContext(loadedModule, textDecoder);
    assert(Felidae::vmValueToDisplayString(vm.executeMain(loadedModule, loadedRuntime), binaryDisplay) ==
           "{answer: 42}");
    const auto malformedPath = testOutputDirectory / "felidae_pipeline_bad.bin";
    { std::ofstream bad(malformedPath, std::ios::binary | std::ios::trunc); bad << "not a felidae IR"; }
    bool malformedRejected = false;
    try { (void)Felidae::loadBinaryIr(malformedPath, Felidae::felidaeSentencePieceModelIdentity()); } catch (const Felidae::IrError&) { malformedRejected = true; }
    assert(malformedRejected);
    const auto expectRejectedCopy = [&](const char* suffix, const auto& mutate) {
        auto candidate = testOutputDirectory / (std::string("felidae_pipeline_") + suffix + ".bin");
        std::filesystem::copy_file(binaryPath, candidate, std::filesystem::copy_options::overwrite_existing);
        mutate(candidate);
        bool rejected = false;
        try { (void)Felidae::loadBinaryIr(candidate, Felidae::felidaeSentencePieceModelIdentity()); } catch (const Felidae::IrError&) { rejected = true; }
        removeTemporary(candidate);
        assert(rejected);
    };
    expectRejectedCopy("bad_version", [](const auto& candidate) {
        std::fstream file(candidate, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(8); const std::array<char, 4> version{2, 0, 0, 0}; file.write(version.data(), version.size());
    });
    expectRejectedCopy("bad_endian", [](const auto& candidate) {
        std::fstream file(candidate, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(12); const std::array<char, 4> endian{}; file.write(endian.data(), endian.size());
    });
    expectRejectedCopy("truncated", [](const auto& candidate) {
        std::filesystem::resize_file(candidate, 20);
    });
    removeTemporary(binaryPath);
    removeTemporary(malformedPath);

    // Primitive normal syntax is directly lowered from the integer parser to
    // canonical IR and evaluated by the register VM, without Interpreter.
    Felidae::IntegerTokenList expressionInput(Felidae::felidaeSentencePieceModel(), "6 * 7");
    Felidae::IntegerParser expressionParser(expressionInput);
    const auto expressionIr = expressionParser.compileExpressionIr();
    class NoRuntime final : public Felidae::VmRuntime {
    public:
        Felidae::VmValue loadSymbol(Felidae::IrSymbolRef symbol) override {
            return symbols.at(symbol);
        }
        void storeSymbol(Felidae::IrSymbolRef symbol, const Felidae::VmValue& value) override {
            symbols[symbol] = value;
        }
        std::unordered_map<Felidae::IrSymbolRef, Felidae::VmValue> symbols;
    } noRuntime;
    Felidae::RegisterVm nativeVm;
    assert(std::get<double>(executeIrDirect(expressionIr, noRuntime)) == 42.0);
    Felidae::IntegerParser astExpressionParser(expressionInput);
    const auto astExpression = astExpressionParser.parseExpressionText();
    const auto astExpressionIr = Felidae::IntegerParser::compileAstExpressionIr(astExpression);
    assert(std::get<double>(executeIrDirect(astExpressionIr, noRuntime)) == 42.0);
    const auto bindingProgram = Felidae::parseProgramText("answer := 6 * 7.");
    assert(bindingProgram.globals.size() == 1);
    const auto bindingIr = Felidae::IntegerParser::compileAstGlobalBindingIr(*bindingProgram.globals.front());
    assert(std::get<double>(executeIrDirect(bindingIr, noRuntime)) == 42.0);
    assert(std::get<double>(noRuntime.symbols.at(Felidae::symbolIdForName("answer"))) == 42.0);
    const auto entryProgram = Felidae::parseProgramText("main() => return (answer: 42).");
    assert(entryProgram.clauses.size() == 1);
    const auto entryIr = Felidae::IntegerParser::compileAstEntryMethodIr(*entryProgram.clauses.front());
    const auto entryResult = std::get<Felidae::VmMapPtr>(executeIrDirect(entryIr, noRuntime));
    assert(entryResult && entryResult->entries.size() == 1);
    assert(std::get<double>(entryResult->entries.front().second) == 42.0);
    auto directFact = std::make_shared<Felidae::MapExpr>(std::vector<Felidae::MapEntry>{
        Felidae::MapEntry{"age", std::make_shared<Felidae::NumberExpr>(4.0)},
    });
    directFact->factType = "Cat";
    const auto directFactIr = Felidae::IntegerParser::compileAstExpressionIr(directFact);
    const auto directFactValue = executeIrDirect(directFactIr, noRuntime);
    const auto directFactResult = std::get<Felidae::VmFactPtr>(directFactValue);
    assert(directFactResult && directFactResult->type == Felidae::symbolIdForName("Cat"));
    assert(directFactResult->fields.size() == 1 &&
           directFactResult->fields.front().first == Felidae::symbolIdForName("age"));
    assert(std::get<double>(directFactResult->fields.front().second) == 4.0);
    const auto directModule = Felidae::compileProgramTextToIr("main() => return (answer: 42).");
    Felidae::IrVerifier::verify(directModule.ir);
    Felidae::FelidaeKnowledgeRuntime directRuntime;
    const auto directResult = executeModuleDirect(directModule, directRuntime);
    const auto directMap = std::get<Felidae::VmMapPtr>(directResult);
    assert(directMap && std::get<double>(directMap->entries.front().second) == 42.0);
    assert(Felidae::vmValueToDisplayString(directResult, display) == "{answer: 42}");

    // Logical operands are control flow, not eager arithmetic. These calls
    // would divide by zero if the right operand were emitted before its
    // guarding branch.
    const auto shortCircuitModule = Felidae::compileProgramTextToIr(
        "explode(divisor: number) => return 1 / divisor.\n"
        "main() => return (both: false and explode(divisor: 0), "
        "either: true or explode(divisor: 0)).\n");
    Felidae::FelidaeKnowledgeRuntime shortCircuitRuntime;
    const auto shortCircuitResult = executeModuleDirect(shortCircuitModule, shortCircuitRuntime);
    assert(Felidae::vmValueToDisplayString(shortCircuitResult, display) ==
           "{both: 0, either: 1}");

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
    const auto repeatedFactsResult = executeModuleDirect(repeatedFactsModule,
                                                              repeatedFactsRuntime);
    assert(Felidae::vmValueToDisplayString(repeatedFactsResult, display) == "[red, blue]");

    // Real source semantic intrinsic -> structured compiler IR
    // operation ID -> typed runtime-model result. No tokenizer or symbol ID
    // crosses the semantic execution boundary.
    const auto semanticModule = Felidae::compileProgramTextToIr(
        "main() => return semantic_identity(42).\n");
    const auto semanticIr = Felidae::verifyIrModule(Felidae::IrModule(semanticModule));
    assert(Felidae::containsRuntimeSemanticOperation(semanticIr));
    class IdentityRuntimeModel final : public Felidae::RuntimeStateModel {
    public:
        Felidae::VmValue evaluate(const Felidae::RuntimeOperation& operation,
                                  std::span<const Felidae::VmValue> inputs,
                                  Felidae::RuntimeContext&) override {
            assert(operation.id == static_cast<std::uint16_t>(Felidae::SemanticOperationId::Identity));
            assert(inputs.size() == 1);
            return inputs.front();
        }
    } identityModel;
    Felidae::FelidaeKnowledgeRuntime semanticRuntime(&identityModel);
    assert(std::get<double>(Felidae::RegisterVm{}.executeMain(semanticIr, semanticRuntime)) == 42.0);
    const auto executeBinaryModule = [&](const Felidae::IrModule& candidate) {
        const auto path = testOutputDirectory / "felidae_pipeline_feature_roundtrip.bin";
        try {
            Felidae::writeBinaryIr(path, Felidae::verifyIrModule(Felidae::IrModule(candidate)));
            const auto loaded = Felidae::loadBinaryIr(path, Felidae::felidaeSentencePieceModelIdentity());
            Felidae::FelidaeKnowledgeRuntime runtime;
            const auto result = nativeVm.executeMain(loaded, runtime);
            std::filesystem::remove(path);
            return Felidae::vmValueToDisplayString(result, display);
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
    assert(Felidae::vmValueToDisplayString(globalsResult, display) == "{answer: 42}");
    assert(executeBinaryModule(globalsModule) == "{answer: 42}");
    const auto textGlobalsModule = Felidae::compileProgramTextToIr(
        "label := \"meow\".\nmain() => return (label: label).");
    Felidae::FelidaeKnowledgeRuntime textGlobalsRuntime;
    const auto textGlobalsResult = executeModuleDirect(textGlobalsModule, textGlobalsRuntime);
    assert(Felidae::vmValueToDisplayString(textGlobalsResult, display) == "{label: meow}");
    assert(executeBinaryModule(textGlobalsModule) == "{label: meow}");
    const auto conditionalModule = Felidae::compileProgramTextToIr(
        "main() =>\nif 3 > 2 then\nreturn (result: \"yes\")\nelse\nreturn (result: \"no\").");
    Felidae::IrVerifier::verify(conditionalModule.ir);
    Felidae::FelidaeKnowledgeRuntime conditionalRuntime;
    const auto conditionalResult = executeModuleDirect(conditionalModule, conditionalRuntime);
    assert(Felidae::vmValueToDisplayString(conditionalResult, display) == "{result: yes}");
    assert(executeBinaryModule(conditionalModule) == "{result: yes}");
    const auto falseConditionalModule = Felidae::compileProgramTextToIr(
        "main() =>\nif 2 > 3 then\nreturn (result: \"yes\")\nelse\nreturn (result: \"no\").");
    Felidae::FelidaeKnowledgeRuntime falseConditionalRuntime;
    const auto falseConditionalResult = executeModuleDirect(falseConditionalModule, falseConditionalRuntime);
    assert(Felidae::vmValueToDisplayString(falseConditionalResult, display) == "{result: no}");
    assert(executeBinaryModule(falseConditionalModule) == "{result: no}");
    const auto localBindingModule = Felidae::compileProgramTextToIr(
        "main() =>\nvalue := 40\nanswer := value + 2\nreturn (answer: answer).");
    Felidae::IrVerifier::verify(localBindingModule.ir);
    Felidae::FelidaeKnowledgeRuntime localBindingRuntime;
    const auto localBindingResult = executeModuleDirect(localBindingModule, localBindingRuntime);
    assert(Felidae::vmValueToDisplayString(localBindingResult, display) == "{answer: 42}");
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
    assert(Felidae::vmValueToDisplayString(callsResult, display) == "{answer: 42}");
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
    assert(Felidae::vmValueToDisplayString(executeModuleDirect(isolatedScopeModule, isolatedScopeRuntime), display) ==
           "{caller: 5, callee: 8}");
    assert(executeBinaryModule(isolatedScopeModule) == "{caller: 5, callee: 8}");
    const auto scopeCompileRejected = [](const std::string& source) {
        try {
            (void)Felidae::compileProgramTextToIr(source);
            return false;
        } catch (const Felidae::IntegerParserError&) {
            return true;
        }
    };
    assert(scopeCompileRejected("value := 1.\nvalue := 2.\nmain() => return value."));
    assert(scopeCompileRejected("value := 1.\nmain() => return value.\nhelper(value: expr) => return value."));
    assert(scopeCompileRejected("main() => return helper(value: 1).\nhelper(value: expr) =>\nvalue := 2\nreturn value."));
    assert(scopeCompileRejected("main() => return system.result.\n"));
    assert(scopeCompileRejected("main() => return helper(value: 1).\nhelper(value: expr, value: expr) => return value."));
    const auto recursionModule = Felidae::compileProgramTextToIr(
        "main() => return (answer: countdown(value: 4)).\n"
        "countdown(value: expr) =>\n"
        "if value == 0 then\n"
        "return 0\n"
        "else\n"
        "return countdown(value: value - 1).");
    Felidae::FelidaeKnowledgeRuntime recursionRuntime(nullptr, 1024, 16);
    const auto recursionResult = executeModuleDirect(recursionModule, recursionRuntime);
    assert(Felidae::vmValueToDisplayString(recursionResult, display) == "{answer: 0}");
    assert(executeBinaryModule(recursionModule) == "{answer: 0}");
    bool undefinedProcedureRejected = false;
    try {
        (void)Felidae::compileProgramTextToIr("main() => return (answer: helper()).");
    } catch (const Felidae::IntegerParserError&) {
        undefinedProcedureRejected = true;
    }
    assert(undefinedProcedureRejected);
    const auto runtimeExpressionIr = Felidae::tryCompileExpressionTextToIr("10 - 3");
    assert(runtimeExpressionIr);
    assert(std::get<double>(executeIrDirect(*runtimeExpressionIr, noRuntime)) == 7.0);
    const auto comparisonIr = Felidae::tryCompileExpressionTextToIr("3 < 7");
    assert(comparisonIr);
    assert(std::get<double>(executeIrDirect(*comparisonIr, noRuntime)) == 1.0);
    const auto logicIr = Felidae::tryCompileExpressionTextToIr("not false and true");
    assert(logicIr);
    assert(std::get<double>(executeIrDirect(*logicIr, noRuntime)) == 1.0);
    const auto moduloIr = Felidae::tryCompileExpressionTextToIr("17 % 5");
    assert(moduloIr);
    assert(std::get<double>(executeIrDirect(*moduloIr, noRuntime)) == 2.0);
    const auto callIr = Felidae::tryCompileExpressionTextToIr("combine(6, 7)");
    assert(callIr);
    Felidae::IrVerifier::verify(*callIr);
    const auto namedCallIr = Felidae::tryCompileExpressionTextToIr("combine(left: 6, right: 7)");
    assert(namedCallIr);
    Felidae::IrVerifier::verify(*namedCallIr);
    const auto textIr = Felidae::tryCompileExpressionTextToIr("\"mixfix text\"");
    assert(textIr);
    assert(Felidae::vmValueToDisplayString(executeIrDirect(*textIr, noRuntime), display) == "mixfix text");
    const auto nilIr = Felidae::tryCompileExpressionTextToIr("nil");
    assert(nilIr);
    assert(std::holds_alternative<Felidae::VmNil>(executeIrDirect(*nilIr, noRuntime)));
    const auto arrayIr = Felidae::tryCompileExpressionTextToIr("[1, 2, 3]");
    assert(arrayIr);
    const auto array = std::get<Felidae::VmArrayPtr>(executeIrDirect(*arrayIr, noRuntime));
    assert(array && array->values.size() == 3 && std::get<double>(array->values[2]) == 3.0);
    const auto mapIr = Felidae::tryCompileExpressionTextToIr("{answer: 42, enabled: true}");
    assert(mapIr);
    const auto map = std::get<Felidae::VmMapPtr>(executeIrDirect(*mapIr, noRuntime));
    assert(map && map->entries.size() == 2);
    assert(std::get<double>(map->entries[0].second) == 42.0);
    assert(std::get<double>(map->entries[1].second) == 1.0);
    class SpanModel final : public Felidae::MixfixStateModel {
    public:
        std::vector<Felidae::MixfixVocabularyId> transform(
            std::span<const Felidae::SentencePieceId> input,
            const Felidae::MixfixContext&) override {
            assert(!input.empty());
            return {0, 1, 2, 3, 4, 2, 2, 5};
        }
    } spanModel;
    Felidae::MixfixContext mixfixContext;
    mixfixContext.constantReferences = {0};
    mixfixContext.outputVocabulary = {
        {Felidae::MixfixIrTokenKind::Accept, 0},
        {Felidae::MixfixIrTokenKind::Opcode, static_cast<Felidae::IrWord>(Felidae::IrOpcode::LoadConst)},
        {Felidae::MixfixIrTokenKind::Register, 0},
        {Felidae::MixfixIrTokenKind::ConstantReference, 0},
        {Felidae::MixfixIrTokenKind::Opcode, static_cast<Felidae::IrWord>(Felidae::IrOpcode::Return)},
        {Felidae::MixfixIrTokenKind::End, 0},
    };
    Felidae::FelidaeIr mixfixShell;
    mixfixShell.registerCount = 1;
    mixfixShell.constants = {Felidae::encodeIrNumber(42.0)};
    Felidae::IntegerTokenList mixfixInput(Felidae::felidaeSentencePieceModel(), "unresolved mixfix span");
    Felidae::IntegerParser mixfixParser(mixfixInput);
    const auto mixfixIr = mixfixParser.compileVerifiedMixfixSpanIr(
        spanModel, mixfixContext, mixfixShell, 0, mixfixInput.entries().size());
    assert(std::get<double>(executeIrDirect(mixfixIr, noRuntime)) == 42.0);
    const auto fieldIr = Felidae::tryCompileExpressionTextToIr("{answer: 42}:answer");
    assert(fieldIr);
    assert(std::get<double>(executeIrDirect(*fieldIr, noRuntime)) == 42.0);
    const auto aggregateEqualityIr = Felidae::tryCompileExpressionTextToIr("[1, {answer: 42}] == [1, {answer: 42}]");
    assert(aggregateEqualityIr);
    assert(std::get<double>(executeIrDirect(*aggregateEqualityIr, noRuntime)) == 1.0);
}
