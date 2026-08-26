#include "IntegerParser.h"
#include "FelidaeIr.h"
#include "IntegerTokenList.h"
#include "Operator.h"
#include "SentencePieceModel.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    const bool nativeExpression = argc == 3 && std::string_view(argv[1]) == "--native-expression";
    const bool dumpIds = argc == 3 && std::string_view(argv[1]) == "--dump-ids";
    if (nativeExpression) {
        const std::string source = argv[2];
        const auto started = std::chrono::steady_clock::now();
        const auto& model = Felidae::felidaeSentencePieceModel();
        const auto modelReady = std::chrono::steady_clock::now();
        Felidae::IntegerTokenList tokens(model, source);
        const auto encoded = std::chrono::steady_clock::now();
        Felidae::IntegerParser parser(tokens);
        const auto ir = parser.compileExpressionIr();
        const auto compiled = std::chrono::steady_clock::now();
        Felidae::IrVerifier::verify(ir);
        const auto verified = std::chrono::steady_clock::now();
        class NoRuntime final : public Felidae::VmRuntime {
        } runtime;
        constexpr Felidae::IrSymbolRef entry = 0xf11da00000000001ull;
        Felidae::IrModule module;
        module.entryProcedure = entry;
        module.ir.registerCount = 1;
        module.ir.symbols = {entry};
        module.ir.words = {
            static_cast<Felidae::IrWord>(Felidae::IrOpcode::Call), 0, 0, 0,
            static_cast<Felidae::IrWord>(Felidae::IrOpcode::Return), 0, 0,
            static_cast<Felidae::IrWord>(Felidae::IrOpcode::End),
        };
        module.procedures.emplace(entry, Felidae::IrProcedure{ir, {}, {}, {}});
        Felidae::RegisterVm vm;
        (void)vm.executeMain(Felidae::verifyIrModule(std::move(module)), runtime, Felidae::VmNil{});
        const auto executed = std::chrono::steady_clock::now();
        const auto micros = [](auto begin, auto end) {
            return std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
        };
        std::cout << "{\"mode\":\"native-expression\""
                  << ",\"tokenCount\":" << tokens.entries().size()
                  << ",\"modelInitMicros\":" << micros(started, modelReady)
                  << ",\"sourceEncodeMicros\":" << micros(modelReady, encoded)
                  << ",\"irCompileMicros\":" << micros(encoded, compiled)
                  << ",\"verifyMicros\":" << micros(compiled, verified)
                  << ",\"vmMicros\":" << micros(verified, executed)
                  << ",\"totalMicros\":" << micros(started, executed) << "}\n";
        return 0;
    }
    const int sourceArgument = dumpIds ? 2 : 1;
    if (argc != 2 && !dumpIds) {
        std::cerr << "usage: felidae_integer_parser_benchmark [--dump-ids] <source.fx>\n"
                  << "       felidae_integer_parser_benchmark --native-expression <expression>\n";
        return 2;
    }
    std::ifstream input(argv[sourceArgument], std::ios::binary);
    if (!input) throw std::runtime_error("cannot open benchmark source");
    const std::string source((std::istreambuf_iterator<char>(input)), {});

    const auto started = std::chrono::steady_clock::now();
    const auto modelStarted = started;
    const auto& model = Felidae::felidaeSentencePieceModel();
    const auto modelReady = std::chrono::steady_clock::now();
    Felidae::IntegerTokenList tokens(model, source);
    const auto encoded = std::chrono::steady_clock::now();
    if (dumpIds) {
        for (const auto& entry : tokens.entries()) {
            std::cout << entry.id << '\t' << entry.begin << '\t' << entry.end << '\t'
                      << Felidae::builtinTokenSpelling(entry.id) << '\n';
        }
        return 0;
    }
    // The benchmark must exercise the same single parser path as runtime:
    // declarations register their compiled ID anchors before later uses in
    // the same source stream are assembled.
    auto operators = std::make_shared<Felidae::OperatorRegistry>();
    Felidae::IntegerParser parser(tokens, std::move(operators));
    const auto program = parser.parseProgram();
    const auto finished = std::chrono::steady_clock::now();
    const auto micros = [](auto begin, auto end) {
        return std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
    };
    const auto& metrics = parser.metrics();
    std::cout << "{\"encodeCount\":" << metrics.sourceEncodeCount
              << ",\"tokenCount\":" << tokens.entries().size()
              << ",\"statementCount\":" << program.statements.size()
              << ",\"modelInitMicros\":" << micros(modelStarted, modelReady)
              << ",\"sourceEncodeMicros\":" << micros(modelReady, encoded)
              << ",\"parseMicros\":" << micros(encoded, finished)
              << ",\"totalMicros\":" << micros(started, finished)
              << ",\"iterations\":" << metrics.iterations
              << ",\"peakRecursionDepth\":" << metrics.peakRecursionDepth
              << ",\"backtrackingAttempts\":" << metrics.backtrackingAttempts
              << "}\n";
}
