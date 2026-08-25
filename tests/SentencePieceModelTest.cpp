#include "IntegerTokenList.h"
#include "IntegerParser.h"
#include "FelidaeGrammar.h"
#include "MixfixStateModel.h"
#include "ModelStore.h"

#include <sentencepiece_processor.h>
#include <sentencepiece.pb.h>

#include <cassert>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {
template <typename Fn>
void assertIntegerParseFails(Fn&& fn) {
    bool failed = false;
    try { fn(); }
    catch (const Felidae::IntegerParserError&) { failed = true; }
    assert(failed);
}
}

int main() {
    // CLI-support helpers remain side-effect free and are covered here rather
    // than by a scratch executable. Windows-native wildcard expansion is
    // required because cmd.exe and PowerShell pass `*.jsonl` literally.
    assert(Felidae::wildcardMatches("*.jsonl", "mixfix-v1.jsonl"));
    assert(Felidae::wildcardMatches("mixfix-*.jsonl", "mixfix-invalid-v1.jsonl"));
    assert(!Felidae::wildcardMatches("*.jsonl", "mixfix-v1.tsv"));
    char train[] = "--train";
    char dataset[] = "datasets/compiler/*.jsonl";
    char store[] = "--store-model";
    char build[] = "build";
    char epochs[] = "--epochs";
    char twelve[] = "12";
    char rate[] = "--learning-rate";
    char learningRate[] = "0.01";
    char* trainingArguments[]{nullptr, train, dataset, store, build, epochs, twelve, rate, learningRate};
    const auto training = Felidae::parseModelTrainingOptions(
        static_cast<int>(std::size(trainingArguments)), trainingArguments);
    assert(training && training->dataset == std::filesystem::path(dataset));
    assert(training->store == std::filesystem::path(build));
    assert(training->epochs == 12 && training->learningRate == 0.01);
    const auto rejectsTrainingOptions = [](int count, char** arguments) {
        try { (void)Felidae::parseModelTrainingOptions(count, arguments); }
        catch (const std::runtime_error&) { return true; }
        return false;
    };
    char malformedRate[] = "0.01junk";
    char* malformedRateArguments[]{nullptr, train, dataset, store, build, rate, malformedRate};
    assert(rejectsTrainingOptions(static_cast<int>(std::size(malformedRateArguments)),
                                  malformedRateArguments));
    char* duplicateEpochArguments[]{nullptr, train, dataset, store, build,
                                    epochs, twelve, epochs, twelve};
    assert(rejectsTrainingOptions(static_cast<int>(std::size(duplicateEpochArguments)),
                                  duplicateEpochArguments));
    const auto buildModel = Felidae::modelStoreDirectory("build", "mixfix-gru");
    const auto distModel = Felidae::modelStoreDirectory("dist", "runtime-gru");
    assert(buildModel.filename() == "mixfix-gru" && buildModel.parent_path().filename() == "build");
    assert(distModel.filename() == "runtime-gru" && distModel.parent_path().filename() == "models");

    // Expansion occurs in the executable, not in the user's shell. Keep the
    // isolated fixture in the configured build/test-artifacts directory; no
    // test may create a random system-temporary build tree.
    const std::filesystem::path datasetTestDirectory(FELIDAE_TEST_OUTPUT_DIR);
    std::error_code ignored;
    std::filesystem::remove_all(datasetTestDirectory, ignored);
    std::filesystem::create_directories(datasetTestDirectory);
    {
        std::ofstream(datasetTestDirectory / "mixfix-v1.jsonl") << "{}\n";
        std::ofstream(datasetTestDirectory / "mixfix-invalid-v1.jsonl") << "{}\n";
        std::ofstream(datasetTestDirectory / "ignored.tsv") << "{}\n";
    }
    const auto expanded = Felidae::expandJsonlDatasetPaths(datasetTestDirectory / "mixfix-*.jsonl");
    assert(expanded.size() == 2);
    assert(expanded[0].filename() == "mixfix-invalid-v1.jsonl");
    assert(expanded[1].filename() == "mixfix-v1.jsonl");
    std::filesystem::remove_all(datasetTestDirectory);

    sentencepiece::SentencePieceProcessor model;
    const auto loaded = model.Load(FELIDAE_SENTENCEPIECE_MODEL_PATH);
    assert(loaded.ok());
    assert(Felidae::kFelidaeTokenizerDatasetSchemaVersion == 1);
    assert(Felidae::kFelidaeTokenizerDatasetHash != 0);
    assert(Felidae::kFelidaeTokenizerDatasetRecordCount == 32);
    assert(Felidae::kFelidaeSentencePieceVocabularySize ==
           static_cast<std::uint32_t>(model.GetPieceSize()));
    assert(Felidae::kFelidaeSentencePieceModelHash != 0);
    assert(model.GetPieceSize() <= 1024 && model.GetPieceSize() > 512);

    // Corpus-covered identifiers should use learned pieces rather than one
    // byte token per source byte. This is a stable detection-efficiency gate,
    // while byte fallback below remains the robustness gate for unseen UTF-8.
    const std::string corpusVocabulary =
        "semantic_identity effective_at commonAncestors for_each_fact RatingProfile";
    sentencepiece::SentencePieceText corpusEncoding;
    assert(model.Encode(corpusVocabulary, &corpusEncoding).ok());
    assert(corpusEncoding.pieces_size() < static_cast<int>(corpusVocabulary.size() / 2));
    for (const auto& piece : corpusEncoding.pieces()) {
        assert(piece.id() != Felidae::TokenId::UNKNOWN);
    }

    for (std::size_t index = 0; index < std::size(Felidae::kBuiltinTokens); ++index) {
        const auto spelling = Felidae::kBuiltinTokens[index].spelling;
        const int id = model.PieceToId(std::string(spelling));
        assert(id == Felidae::kFelidaeBuiltinSentencePieceIds[index]);
        assert(model.IdToPiece(id) == spelling);

        sentencepiece::SentencePieceText encoded;
        const auto status = model.Encode(std::string(spelling), &encoded);
        assert(status.ok());
        assert(encoded.pieces_size() == 1);
        assert(encoded.pieces(0).id() == id);
        assert(encoded.pieces(0).begin() == 0);
        assert(encoded.pieces(0).end() == static_cast<int>(spelling.size()));
    }

    // Capitalization is a source property even when SentencePiece places its
    // whitespace marker and several bytes in the first identifier piece.
    // This keeps capitalized, named terms on the fact-value path after an
    // indented return.
    const Felidae::IntegerTokenList indentedFactTokens(model, "    Stage(depth: 1)");
    Felidae::IntegerParser indentedFactParser(indentedFactTokens);
    const auto indentedFact = std::dynamic_pointer_cast<Felidae::TermExpr>(
        indentedFactParser.parseExpressionText());
    assert(indentedFact && indentedFact->isCapitalized);


    // Arbitrary source spelling must remain distinguishable as integer
    // sequences. Byte fallback prevents a pair of user anchors from both
    // degenerating to SentencePiece's unknown ID.
    std::vector<int> wraps;
    std::vector<int> accepts;
    std::vector<int> unicode;
    for (const std::string& spelling : {std::string("wraps"), std::string("accepts"), std::string("naïve")}) {
        sentencepiece::SentencePieceText encoded;
        assert(model.Encode(spelling, &encoded).ok());
        std::vector<int> ids;
        for (const auto& piece : encoded.pieces()) {
            assert(piece.id() != Felidae::TokenId::UNKNOWN);
            ids.push_back(piece.id());
        }
        if (spelling == "wraps") wraps = std::move(ids);
        else if (spelling == "accepts") accepts = std::move(ids);
        else unicode = std::move(ids);
    }
    assert(!wraps.empty() && !accepts.empty() && !unicode.empty());
    assert(wraps != accepts);

    const std::string completeSource =
        "# comment\\n"
        "café(value: \"hello\") => return value\\n";
    const Felidae::IntegerTokenList sourceTokens(model, completeSource);
    assert(sourceTokens.encodeCount() == 1);
    assert(!sourceTokens.entries().empty());
    for (const auto& entry : sourceTokens.entries()) {
        assert(entry.begin <= entry.end);
        assert(entry.end <= completeSource.size());
    }

    const Felidae::IntegerTokenList expressionTokens(
        model, "# ignored\n[\"value\", café, (true)]");
    Felidae::IntegerParser integerParser(expressionTokens);
    const auto expression = integerParser.parseExpressionText();
    assert(expression->debug() == "[\"value\", café, true]");
    assert(integerParser.metrics().sourceEncodeCount == 1);
    assert(integerParser.metrics().tokenCount == expressionTokens.entries().size());
    assert(integerParser.metrics().iterations > 0);
    assert(integerParser.metrics().peakRecursionDepth > 0);

    const Felidae::IntegerTokenList structuredTokens(
        model, "# direct integer grammar\nworker(task: {name: \"café\"}).result + 2 * 3");
    Felidae::IntegerParser structuredParser(structuredTokens);
    const auto structured = structuredParser.parseExpressionText();
    assert(structured->debug() == "worker(task: {name: \"café\"}):result + 2 * 3");
    assert(structuredParser.metrics().sourceEncodeCount == 1);

    const Felidae::IntegerTokenList programTokens(
        model, "import \"core.fx\".\nthreshold := 2 + 3.\nPerson(name: \"Ada\", age: threshold).");
    Felidae::IntegerParser programParser(programTokens);
    const auto program = programParser.parseProgram();
    assert(program.imports.size() == 1);
    assert(program.globals.size() == 1);
    assert(program.clauses.size() == 1);
    assert(program.clauses.front()->head.name == "Person");

    // Qualified declaration heads are assembled from a single ID stream.  In
    // particular, native declarations such as `math.sin` must not leave the
    // DOT ID behind as a second pseudo-statement.
    const Felidae::IntegerTokenList qualifiedHeadTokens(
        model, "math.sin(value: number) => ().\nsystem.print(value: any) => ().\n");
    Felidae::IntegerParser qualifiedHeadParser(qualifiedHeadTokens);
    const auto qualifiedHeadProgram = qualifiedHeadParser.parseProgram();
    assert(qualifiedHeadProgram.clauses.size() == 2);
    assert(qualifiedHeadProgram.clauses[0]->head.name == "math.sin");
    assert(qualifiedHeadProgram.clauses[1]->head.name == "system.print");
    assert(qualifiedHeadProgram.clauses[0]->head.builtinId == Felidae::BuiltinId::MathSin);
    assert(qualifiedHeadProgram.clauses[1]->head.builtinId == Felidae::BuiltinId::SystemPrint);
    assert(qualifiedHeadProgram.clauses[0]->head.nameId != 0);
    assert(qualifiedHeadProgram.clauses[1]->head.nameId != 0);

    const Felidae::IntegerTokenList qualifiedCallTokens(model, "math.sin(value: 0)");
    Felidae::IntegerParser qualifiedCallParser(qualifiedCallTokens);
    const auto qualifiedCall = std::dynamic_pointer_cast<Felidae::TermExpr>(
        qualifiedCallParser.parseExpressionText());
    assert(qualifiedCall && qualifiedCall->builtinId == Felidae::BuiltinId::MathSin);
    assert(qualifiedCall->nameId != 0);

    const Felidae::IntegerTokenList repeatedTokens(
        model, "Record(index: 0).\nRecord(index: 1).\n");
    Felidae::IntegerParser repeatedParser(repeatedTokens);
    const auto repeatedProgram = repeatedParser.parseProgram();
    assert(repeatedProgram.clauses.size() == 2);

    // `as` is an atomic grammar ID, but may also appear as an integer piece
    // inside a longer identifier.  Context and offsets must keep all three
    // valid forms distinct without source-character matching.
    const Felidae::IntegerTokenList designationTokens(
        model, "Assessment(as: \"field\") as audited, reviewed\n");
    Felidae::IntegerParser designationParser(designationTokens);
    const auto designationProgram = designationParser.parseProgram();
    assert(designationProgram.clauses.size() == 1);
    assert(designationProgram.clauses.front()->head.name == "Assessment");
    assert(designationProgram.clauses.front()->head.args.front().name == "as");
    assert(designationProgram.clauses.front()->designationIds.size() == 2);
    const Felidae::IntegerTokenList designationQueryTokens(
        model, "? Assessment(as: \"field\") as audited");
    Felidae::IntegerParser designationQueryParser(designationQueryTokens);
    const auto designationGoals = designationQueryParser.parseQuery();
    const auto designationGoal = std::dynamic_pointer_cast<Felidae::CallGoal>(
        designationGoals.front());
    assert(designationGoal && designationGoal->call.designationIds.size() == 1);

    const Felidae::IntegerTokenList dottedLabelTokens(
        model, "Event(fx.effective_at: \"2025-01-01\")\n");
    Felidae::IntegerParser dottedLabelParser(dottedLabelTokens);
    const auto dottedLabelProgram = dottedLabelParser.parseProgram();
    assert(dottedLabelProgram.clauses.size() == 1);
    assert(dottedLabelProgram.clauses.front()->head.args.front().name == "fx.effective_at");

    // Moderate-size program regression: a full source is encoded exactly once
    // and repeated SentencePiece word fragments never merge statements.
    std::string largeSource;
    largeSource.reserve(12'000);
    for (int index = 0; index < 512; ++index) {
        largeSource += "Record(index: " + std::to_string(index) + ").\n";
    }
    const Felidae::IntegerTokenList largeTokens(model, largeSource);
    Felidae::IntegerParser largeParser(largeTokens);
    const auto largeProgram = largeParser.parseProgram();
    assert(largeProgram.clauses.size() == 512);
    assert(largeParser.metrics().sourceEncodeCount == 1);
    assert(largeParser.metrics().statementCount == 512);
    assert(largeParser.metrics().iterations < 100'000);

    const Felidae::IntegerTokenList methodTokens(
        model, "main() =>\n    value := 1\n    return (answer: value)\n");
    Felidae::IntegerParser methodParser(methodTokens);
    const auto methodProgram = methodParser.parseProgram();
    assert(methodProgram.clauses.size() == 1);
    assert(methodProgram.clauses.front()->clauseKind == Felidae::ClauseKind::Method);

    const Felidae::IntegerTokenList bareReturnBoundaryTokens(
        model, "first() =>\n    return\nsecond() => return (value: 1)\n");
    Felidae::IntegerParser bareReturnBoundaryParser(bareReturnBoundaryTokens);
    const auto bareReturnBoundaryProgram = bareReturnBoundaryParser.parseProgram();
    assert(bareReturnBoundaryProgram.clauses.size() == 2);

    const Felidae::IntegerTokenList queryTokens(
        model, "? AncestorOf(descendant: \"kitten\", ancestor: Ancestor)");
    Felidae::IntegerParser queryParser(queryTokens);
    const auto queryGoals = queryParser.parseQuery();
    assert(queryGoals.size() == 1);

    const std::string queryText = "? AncestorOf(descendant: \"kitten\", ancestor: Ancestor)";
    const Felidae::IntegerTokenList decodedQueryTokens(
        model, "\"? AncestorOf(descendant: \\\"kitten\\\", ancestor: Ancestor)\"");
    Felidae::IntegerParser decodedQueryParser(decodedQueryTokens);
    const auto decodedQuery = std::dynamic_pointer_cast<Felidae::StringExpr>(
        decodedQueryParser.parseExpressionText());
    assert(decodedQuery && decodedQuery->value == queryText);

    const Felidae::IntegerTokenList conditionalTokens(
        model, "choose(value: number) =>\n    if value > 0 then\n        return (result: true)\n    else\n        return (result: false)\n");
    Felidae::IntegerParser conditionalParser(conditionalTokens);
    const auto conditionalProgram = conditionalParser.parseProgram();
    assert(conditionalProgram.clauses.size() == 1);
    assert(!conditionalProgram.clauses.front()->body.empty());

    const Felidae::IntegerTokenList fallbackTokens(
        model, "choose() =>\n    return (result: true)\nelse\n    return (result: false)\n");
    Felidae::IntegerParser fallbackParser(fallbackTokens);
    const auto fallbackProgram = fallbackParser.parseProgram();
    assert(fallbackProgram.clauses.size() == 1);
    assert(fallbackProgram.clauses.front()->fallbackBranches.size() == 1);

    // Annotation syntax is assembled from the same integer stream. Literal
    // anchors retain their spelling but deliberately do not trigger a second
    // SentencePiece encode; matching advances only through full-source
    // encode offsets.
    auto operators = std::make_shared<Felidae::OperatorRegistry>();
    const Felidae::IntegerTokenList mixfixDeclarationTokens(model,
        "@mixfix(pattern: \"choose {value: expr}\")\n"
        "choose() => return (value)\n");
    Felidae::IntegerParser mixfixDeclarationParser(mixfixDeclarationTokens, operators);
    const auto mixfixDeclarationProgram = mixfixDeclarationParser.parseProgram();
    assert(mixfixDeclarationProgram.clauses.size() == 1);
    assert(operators->patterns().size() == 1);
    assert(!operators->patterns().front().anchorLexemes.empty());
    assert(operators->patterns().front().anchorLexemes.front().front().spelling == "choose");
    assert(operators->patterns().front().anchorLexemes.front().front().pieceIds.empty());
    const Felidae::IntegerTokenList mixfixUseTokens(model, "choose 7");
    Felidae::IntegerParser mixfixUseParser(mixfixUseTokens, operators);
    const auto mixfixUse = std::dynamic_pointer_cast<Felidae::OperatorExpression>(
        mixfixUseParser.parseExpressionText());
    assert(mixfixUse);
    assert(mixfixUse->patternId == operators->patterns().front().patternId);
    assert(mixfixUse->captureCount() == 1);
    // Literal anchors cannot consume an identifier prefix after SentencePiece
    // fragmentation: `choose` is not an operator occurrence in `chooseable`.
    assertIntegerParseFails([&] {
        Felidae::IntegerTokenList prefixedAnchor(model, "chooseable 7");
        Felidae::IntegerParser parser(prefixedAnchor, operators);
        (void)parser.parseExpressionText();
    });


    const Felidae::IntegerTokenList nestedMixfixDeclarationTokens(model,
        "@mixfix(pattern: \"wrap {value: expr} end\")\n"
        "wrap() => return (value)\n");
    Felidae::IntegerParser nestedMixfixDeclarationParser(nestedMixfixDeclarationTokens, operators);
    (void)nestedMixfixDeclarationParser.parseProgram();
    const Felidae::IntegerTokenList nestedMixfixUseTokens(model, "wrap choose 7 end");
    Felidae::IntegerParser nestedMixfixUseParser(nestedMixfixUseTokens, operators);
    const auto nestedMixfixUse = std::dynamic_pointer_cast<Felidae::OperatorExpression>(
        nestedMixfixUseParser.parseExpressionText());
    assert(nestedMixfixUse);
    assert(nestedMixfixUse->captureCount() == 1);
    assert(std::dynamic_pointer_cast<Felidae::OperatorExpression>(nestedMixfixUse->capture(0)));

    const Felidae::IntegerTokenList trailingMixfixDeclarationTokens(model,
        "@mixfix(pattern: \"{left: expr} combines {right: expr}\")\n"
        "combines() => return (left)\n");
    Felidae::IntegerParser trailingMixfixDeclarationParser(trailingMixfixDeclarationTokens, operators);
    (void)trailingMixfixDeclarationParser.parseProgram();
    const Felidae::IntegerTokenList trailingMixfixUseTokens(model, "1 combines 2");
    Felidae::IntegerParser trailingMixfixUseParser(trailingMixfixUseTokens, operators);
    const auto trailingMixfixUse = std::dynamic_pointer_cast<Felidae::OperatorExpression>(
        trailingMixfixUseParser.parseExpressionText());
    assert(trailingMixfixUse);
    assert(trailingMixfixUse->captureCount() == 2);

    // Multiple patterns may share an initial anchor.  Candidate assembly is
    // bounded and selects the pattern that consumes the longest valid ID
    // range instead of treating the shared anchor as a string ambiguity.
    auto sameAnchorOperators = std::make_shared<Felidae::OperatorRegistry>();
    const Felidae::IntegerTokenList sameAnchorTokens(model,
        "@mixfix(pattern: \"plan {name: string} using {strategy: string} with {budget: number}\")\n"
        "longPlan() => return (value: name)\n"
        "@mixfix(pattern: \"plan {value: number} using {enabled: bool}\")\n"
        "shortPlan() => return (value: value)\n"
        "main() => return (long: plan \"x\" using \"y\" with 1, short: plan 7 using true)\n");
    Felidae::IntegerParser sameAnchorParser(sameAnchorTokens, sameAnchorOperators);
    const auto sameAnchorProgram = sameAnchorParser.parseProgram();
    assert(sameAnchorProgram.clauses.size() == 3);
    assert(sameAnchorParser.metrics().backtrackingAttempts > 0);


    // Malformed and deliberately deep input must fail cleanly under the
    // parser's forward-progress and recursion bounds.
    assertIntegerParseFails([&] {
        Felidae::IntegerTokenList malformed(model, "Person(name: ");
        Felidae::IntegerParser parser(malformed);
        (void)parser.parseProgram();
    });
    std::string deeplyNested(513, '(');
    deeplyNested += "value";
    deeplyNested.append(513, ')');
    assertIntegerParseFails([&] {
        Felidae::IntegerTokenList nested(model, deeplyNested);
        Felidae::IntegerParser parser(nested);
        (void)parser.parseExpressionText();
    });

    std::string longIdentifier(8192, 'x');
    const Felidae::IntegerTokenList longIdentifierTokens(model, longIdentifier);
    Felidae::IntegerParser longIdentifierParser(longIdentifierTokens);
    const auto longIdentifierExpr = longIdentifierParser.parseExpressionText();
    assert(longIdentifierExpr->debug() == longIdentifier);


    std::cout << "felidae SentencePiece model validation passed\n";
}
