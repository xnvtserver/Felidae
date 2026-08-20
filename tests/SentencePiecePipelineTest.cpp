#include "CompilerFrontend.h"
#include "FelidaeIr.h"
#include "form/BinaryIr.h"
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
#include <variant>
#include <unordered_map>

#include <sentencepiece_processor.h>

int main() {
    const auto removeTemporary = [](const std::filesystem::path& path) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    };
    Felidae::setVmTextDecoder([](std::span<const std::uint32_t> pieces) {
        std::vector<int> ids(pieces.begin(), pieces.end());
        std::string text;
        const auto status = Felidae::felidaeSentencePieceModel().Decode(ids, &text);
        assert(status.ok());
        return text;
    });
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
    // same full-source SentencePiece stream. This must parse even when the
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
    Felidae::RegisterVm directMixfixVm;
    Felidae::DirectVmRuntime directMixfixRuntime(directMixfixModule.procedures);
    assert(std::get<double>(directMixfixVm.executeMain(directMixfixModule,
                                                        directMixfixRuntime)) == 42.0);

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
    Felidae::DirectVmRuntime nestedMixfixRuntime(nestedMixfixModule.procedures);
    const auto nestedMixfixResult = std::get<Felidae::VmMapPtr>(
        directMixfixVm.executeMain(nestedMixfixModule, nestedMixfixRuntime));
    assert(nestedMixfixResult && nestedMixfixResult->entries.size() == 3);
    assert(std::get<double>(nestedMixfixResult->entries[0].second) == 9.0);
    assert(std::get<double>(nestedMixfixResult->entries[1].second) == 36.0);
    assert(std::get<double>(nestedMixfixResult->entries[2].second) == 756.0);

    // The public execution path is parser -> verified IR -> register VM.
    // This test deliberately does not construct Interpreter or a legacy
    // runtime, so a passing pipeline cannot hide an AST execution fallback.
    const auto module = Felidae::compileProgramFileToIr(FELIDAE_PIPELINE_FIXTURE_PATH);
    // IrVerifier decodes instruction boundaries. Do not scan raw words for
    // opcode values: legal registers, constants, and symbols may share a
    // numeric value with an opcode.
    Felidae::IrVerifier::verify(module.ir);
    const auto linkedModule = Felidae::linkIrModule(module);
    assert(!linkedModule.code.empty());
    assert(linkedModule.procedures.size() == module.procedures.size());
    const auto materializedModule = Felidae::materializeIrModule(linkedModule);
    assert(materializedModule.entryProcedure == module.entryProcedure);
    Felidae::RegisterVm vm;
    Felidae::DirectVmRuntime moduleRuntime(module.procedures);
    const auto vmResult = vm.executeMain(module, moduleRuntime);
    assert(Felidae::vmValueToDisplayString(vmResult) == "{answer: 42}");

    // The serialized artifact is executable in a fresh module/runtime path:
    // loader verification occurs before RegisterVm sees any instruction.
    const auto binaryPath = std::filesystem::temp_directory_path() / "felidae_pipeline_roundtrip.bin";
    Felidae::writeBinaryIr(binaryPath, module);
    const auto loadedModule = Felidae::loadBinaryIr(binaryPath);
    Felidae::DirectVmRuntime loadedRuntime(loadedModule.procedures);
    assert(Felidae::vmValueToDisplayString(vm.executeMain(loadedModule, loadedRuntime)) ==
           "{answer: 42}");
    const auto malformedPath = std::filesystem::temp_directory_path() / "felidae_pipeline_bad.bin";
    { std::ofstream bad(malformedPath, std::ios::binary | std::ios::trunc); bad << "not a felidae IR"; }
    bool malformedRejected = false;
    try { (void)Felidae::loadBinaryIr(malformedPath); } catch (const Felidae::IrError&) { malformedRejected = true; }
    assert(malformedRejected);
    const auto badOffsetPath = std::filesystem::temp_directory_path() / "felidae_pipeline_bad_offset.bin";
    std::filesystem::copy_file(binaryPath, badOffsetPath, std::filesystem::copy_options::overwrite_existing);
    // The first section offset starts after magic/version/endian/entry: byte 24.
    // A zero offset must be rejected before any section allocation or parsing.
    { std::fstream badOffset(badOffsetPath, std::ios::binary | std::ios::in | std::ios::out);
      badOffset.seekp(24); const std::array<char, 4> zero{}; badOffset.write(zero.data(), zero.size()); }
    bool badOffsetRejected = false;
    try { (void)Felidae::loadBinaryIr(badOffsetPath); } catch (const Felidae::IrError&) { badOffsetRejected = true; }
    assert(badOffsetRejected);

    const auto expectRejectedCopy = [&](const char* suffix, const auto& mutate) {
        auto candidate = std::filesystem::temp_directory_path() / (std::string("felidae_pipeline_") + suffix + ".bin");
        std::filesystem::copy_file(binaryPath, candidate, std::filesystem::copy_options::overwrite_existing);
        mutate(candidate);
        bool rejected = false;
        try { (void)Felidae::loadBinaryIr(candidate); } catch (const Felidae::IrError&) { rejected = true; }
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
    expectRejectedCopy("legacy_opcode", [](const auto& candidate) {
        // Code section offset is the seventh offset/count pair at byte 72.
        std::ifstream input(candidate, std::ios::binary); input.seekg(72);
        std::array<unsigned char, 4> encodedOffset{}; input.read(reinterpret_cast<char*>(encodedOffset.data()), encodedOffset.size());
        const std::uint32_t codeOffset = static_cast<std::uint32_t>(encodedOffset[0]) |
            (static_cast<std::uint32_t>(encodedOffset[1]) << 8) |
            (static_cast<std::uint32_t>(encodedOffset[2]) << 16) |
            (static_cast<std::uint32_t>(encodedOffset[3]) << 24);
        std::fstream output(candidate, std::ios::binary | std::ios::in | std::ios::out);
        output.seekp(codeOffset); const std::array<char, 8> executeProgram{22, 0, 0, 0, 0, 0, 0, 0};
        output.write(executeProgram.data(), executeProgram.size());
    });
    removeTemporary(binaryPath);
    removeTemporary(malformedPath);
    removeTemporary(badOffsetPath);

    // Primitive normal syntax is directly lowered from the integer parser to
    // canonical IR and evaluated by the register VM, without Interpreter.
    Felidae::IntegerTokenList expressionInput(Felidae::felidaeSentencePieceModel(), "6 * 7");
    Felidae::IntegerParser expressionParser(expressionInput);
    const auto expressionIr = expressionParser.compileExpressionIr();
    class NoRuntime final : public Felidae::VmRuntime {
    public:
        Felidae::VmValue callSymbol(Felidae::IrSymbolRef, std::span<const Felidae::VmValue> arguments) override {
            double total = 0.0;
            for (const auto& argument : arguments) total += std::get<double>(argument);
            return total;
        }
        Felidae::VmValue callSymbolNamed(Felidae::IrSymbolRef,
                                         std::span<const Felidae::VmCallArgument> arguments) override {
            double total = 0.0;
            for (const auto& argument : arguments) total += std::get<double>(argument.value);
            return total;
        }
        Felidae::VmValue loadSymbol(Felidae::IrSymbolRef symbol) override {
            return symbols.at(symbol);
        }
        void storeSymbol(Felidae::IrSymbolRef symbol, const Felidae::VmValue& value) override {
            symbols[symbol] = value;
        }
        std::unordered_map<Felidae::IrSymbolRef, Felidae::VmValue> symbols;
    } noRuntime;
    Felidae::RegisterVm nativeVm;
    assert(std::get<double>(nativeVm.execute(expressionIr, noRuntime, Felidae::VmNil{})) == 42.0);
    Felidae::IntegerParser astExpressionParser(expressionInput);
    const auto astExpression = astExpressionParser.parseExpressionText();
    const auto astExpressionIr = Felidae::IntegerParser::compileAstExpressionIr(astExpression);
    assert(std::get<double>(nativeVm.execute(astExpressionIr, noRuntime, Felidae::VmNil{})) == 42.0);
    const auto bindingProgram = Felidae::parseProgramText("answer := 6 * 7.");
    assert(bindingProgram.globals.size() == 1);
    const auto bindingIr = Felidae::IntegerParser::compileAstGlobalBindingIr(*bindingProgram.globals.front());
    assert(std::get<double>(nativeVm.execute(bindingIr, noRuntime, Felidae::VmNil{})) == 42.0);
    assert(std::get<double>(noRuntime.symbols.at(Felidae::symbolIdForName("answer"))) == 42.0);
    const auto entryProgram = Felidae::parseProgramText("main() => return (answer: 42).");
    assert(entryProgram.clauses.size() == 1);
    const auto entryIr = Felidae::IntegerParser::compileAstEntryMethodIr(*entryProgram.clauses.front());
    const auto entryResult = std::get<Felidae::VmMapPtr>(nativeVm.execute(entryIr, noRuntime, Felidae::VmNil{}));
    assert(entryResult && entryResult->entries.size() == 1);
    assert(std::get<double>(entryResult->entries.front().second) == 42.0);
    auto directFact = std::make_shared<Felidae::MapExpr>(std::vector<Felidae::MapEntry>{
        Felidae::MapEntry{"age", std::make_shared<Felidae::NumberExpr>(4.0)},
    });
    directFact->factType = "Cat";
    const auto directFactIr = Felidae::IntegerParser::compileAstExpressionIr(directFact);
    const auto directFactValue = nativeVm.execute(directFactIr, noRuntime, Felidae::VmNil{});
    const auto directFactResult = std::get<Felidae::VmFactPtr>(directFactValue);
    assert(directFactResult && directFactResult->type == Felidae::symbolIdForName("Cat"));
    assert(directFactResult->fields.size() == 1 &&
           directFactResult->fields.front().first == Felidae::symbolIdForName("age"));
    assert(std::get<double>(directFactResult->fields.front().second) == 4.0);
    const auto directModule = Felidae::compileProgramTextToIr("main() => return (answer: 42).");
    Felidae::IrVerifier::verify(directModule.ir);
    Felidae::DirectVmRuntime directRuntime(directModule.procedures);
    const auto directResult = nativeVm.execute(directModule.ir, directRuntime, Felidae::VmNil{});
    const auto directMap = std::get<Felidae::VmMapPtr>(directResult);
    assert(directMap && std::get<double>(directMap->entries.front().second) == 42.0);
    assert(Felidae::vmValueToDisplayString(directResult) == "{answer: 42}");
    const auto executeBinaryModule = [&](const Felidae::IrModule& candidate) {
        const auto path = std::filesystem::temp_directory_path() / "felidae_pipeline_feature_roundtrip.bin";
        try {
            Felidae::writeBinaryIr(path, candidate);
            const auto loaded = Felidae::loadBinaryIr(path);
            Felidae::DirectVmRuntime runtime(loaded.procedures);
            const auto result = nativeVm.executeMain(loaded, runtime);
            std::filesystem::remove(path);
            return Felidae::vmValueToDisplayString(result);
        } catch (...) {
            std::filesystem::remove(path);
            throw;
        }
    };
    assert(executeBinaryModule(directModule) == "{answer: 42}");
    const auto globalsModule = Felidae::compileProgramTextToIr(
        "answer := 6 * 7.\nmain() => return (answer: answer).");
    Felidae::IrVerifier::verify(globalsModule.ir);
    Felidae::DirectVmRuntime globalsRuntime(globalsModule.procedures);
    const auto globalsResult = nativeVm.execute(globalsModule.ir, globalsRuntime, Felidae::VmNil{});
    assert(Felidae::vmValueToDisplayString(globalsResult) == "{answer: 42}");
    assert(executeBinaryModule(globalsModule) == "{answer: 42}");
    const auto textGlobalsModule = Felidae::compileProgramTextToIr(
        "label := \"meow\".\nmain() => return (label: label).");
    Felidae::DirectVmRuntime textGlobalsRuntime(textGlobalsModule.procedures);
    const auto textGlobalsResult = nativeVm.execute(textGlobalsModule.ir, textGlobalsRuntime, Felidae::VmNil{});
    assert(Felidae::vmValueToDisplayString(textGlobalsResult) == "{label: meow}");
    assert(executeBinaryModule(textGlobalsModule) == "{label: meow}");
    const auto conditionalModule = Felidae::compileProgramTextToIr(
        "main() =>\nif 3 > 2 then\nreturn (result: \"yes\")\nelse\nreturn (result: \"no\").");
    Felidae::IrVerifier::verify(conditionalModule.ir);
    Felidae::DirectVmRuntime conditionalRuntime(conditionalModule.procedures);
    const auto conditionalResult = nativeVm.execute(conditionalModule.ir, conditionalRuntime, Felidae::VmNil{});
    assert(Felidae::vmValueToDisplayString(conditionalResult) == "{result: yes}");
    assert(executeBinaryModule(conditionalModule) == "{result: yes}");
    const auto falseConditionalModule = Felidae::compileProgramTextToIr(
        "main() =>\nif 2 > 3 then\nreturn (result: \"yes\")\nelse\nreturn (result: \"no\").");
    Felidae::DirectVmRuntime falseConditionalRuntime(falseConditionalModule.procedures);
    const auto falseConditionalResult = nativeVm.execute(falseConditionalModule.ir, falseConditionalRuntime, Felidae::VmNil{});
    assert(Felidae::vmValueToDisplayString(falseConditionalResult) == "{result: no}");
    assert(executeBinaryModule(falseConditionalModule) == "{result: no}");
    const auto localBindingModule = Felidae::compileProgramTextToIr(
        "main() =>\nvalue := 40\nanswer := value + 2\nreturn (answer: answer).");
    Felidae::IrVerifier::verify(localBindingModule.ir);
    Felidae::DirectVmRuntime localBindingRuntime(localBindingModule.procedures);
    const auto localBindingResult = nativeVm.execute(localBindingModule.ir, localBindingRuntime, Felidae::VmNil{});
    assert(Felidae::vmValueToDisplayString(localBindingResult) == "{answer: 42}");
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
    Felidae::DirectVmRuntime callsRuntime(callsModule.procedures);
    const auto callsResult = nativeVm.execute(callsModule.ir, callsRuntime, Felidae::VmNil{});
    assert(Felidae::vmValueToDisplayString(callsResult) == "{answer: 42}");
    assert(executeBinaryModule(callsModule) == "{answer: 42}");
    const auto recursionModule = Felidae::compileProgramTextToIr(
        "main() => return (answer: countdown(value: 4)).\n"
        "countdown(value: expr) =>\n"
        "if value == 0 then\n"
        "return 0\n"
        "else\n"
        "return countdown(value: value - 1).");
    Felidae::DirectVmRuntime recursionRuntime(recursionModule.procedures, nullptr, 1024, 16);
    const auto recursionResult = nativeVm.execute(recursionModule.ir, recursionRuntime, Felidae::VmNil{});
    assert(Felidae::vmValueToDisplayString(recursionResult) == "{answer: 0}");
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
    assert(std::get<double>(nativeVm.execute(*runtimeExpressionIr, noRuntime, Felidae::VmNil{})) == 7.0);
    const auto comparisonIr = Felidae::tryCompileExpressionTextToIr("3 < 7");
    assert(comparisonIr);
    assert(std::get<bool>(nativeVm.execute(*comparisonIr, noRuntime, Felidae::VmNil{})));
    const auto logicIr = Felidae::tryCompileExpressionTextToIr("not false and true");
    assert(logicIr);
    assert(std::get<bool>(nativeVm.execute(*logicIr, noRuntime, Felidae::VmNil{})));
    const auto moduloIr = Felidae::tryCompileExpressionTextToIr("17 % 5");
    assert(moduloIr);
    assert(std::get<double>(nativeVm.execute(*moduloIr, noRuntime, Felidae::VmNil{})) == 2.0);
    const auto callIr = Felidae::tryCompileExpressionTextToIr("combine(6, 7)");
    assert(callIr);
    assert(std::get<double>(nativeVm.execute(*callIr, noRuntime, Felidae::VmNil{})) == 13.0);
    const auto namedCallIr = Felidae::tryCompileExpressionTextToIr("combine(left: 6, right: 7)");
    assert(namedCallIr);
    assert(std::get<double>(nativeVm.execute(*namedCallIr, noRuntime, Felidae::VmNil{})) == 13.0);
    const auto textIr = Felidae::tryCompileExpressionTextToIr("\"mixfix text\"");
    assert(textIr);
    assert(Felidae::vmValueToDisplayString(nativeVm.execute(*textIr, noRuntime, Felidae::VmNil{})) == "mixfix text");
    const auto nilIr = Felidae::tryCompileExpressionTextToIr("nil");
    assert(nilIr);
    assert(std::holds_alternative<Felidae::VmNil>(nativeVm.execute(*nilIr, noRuntime, Felidae::VmNil{})));
    const auto arrayIr = Felidae::tryCompileExpressionTextToIr("[1, 2, 3]");
    assert(arrayIr);
    const auto array = std::get<Felidae::VmArrayPtr>(nativeVm.execute(*arrayIr, noRuntime, Felidae::VmNil{}));
    assert(array && array->values.size() == 3 && std::get<double>(array->values[2]) == 3.0);
    const auto mapIr = Felidae::tryCompileExpressionTextToIr("{answer: 42, enabled: true}");
    assert(mapIr);
    const auto map = std::get<Felidae::VmMapPtr>(nativeVm.execute(*mapIr, noRuntime, Felidae::VmNil{}));
    assert(map && map->entries.size() == 2);
    assert(std::get<double>(map->entries[0].second) == 42.0);
    assert(std::get<bool>(map->entries[1].second));
    class SpanModel final : public Felidae::MixfixStateModel {
    public:
        std::vector<Felidae::MixfixVocabularyId> transform(
            std::span<const Felidae::SentencePieceId> input,
            const Felidae::MixfixContext&) override {
            assert(!input.empty());
            return {0, 1, 2, 3, 1, 1, 4};
        }
    } spanModel;
    Felidae::MixfixContext mixfixContext;
    mixfixContext.constantReferences = {0};
    mixfixContext.outputVocabulary = {
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
    assert(std::get<double>(nativeVm.execute(mixfixIr, noRuntime, Felidae::VmNil{})) == 42.0);
    const auto fieldIr = Felidae::tryCompileExpressionTextToIr("{answer: 42}:answer");
    assert(fieldIr);
    assert(std::get<double>(nativeVm.execute(*fieldIr, noRuntime, Felidae::VmNil{})) == 42.0);
    const auto aggregateEqualityIr = Felidae::tryCompileExpressionTextToIr("[1, {answer: 42}] == [1, {answer: 42}]");
    assert(aggregateEqualityIr);
    assert(std::get<bool>(nativeVm.execute(*aggregateEqualityIr, noRuntime, Felidae::VmNil{})));
}
