#include "CompilerFrontend.h"
#include "IntegerParser.h"
#include "IntegerTokenList.h"
#include "IrCodeGenerator.h"
#include "MixfixStateModel.h"
#include "SentencePieceModel.h"
#include "form/RegisterVm.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace Felidae;

std::size_t byteOffset(const std::string &source, int line, int column) {
  if (line < 1 || column < 1)
    throw std::runtime_error("mixfix AST span is invalid");
  std::size_t offset = 0;
  for (int current = 1; current < line; ++current) {
    const auto newline = source.find('\n', offset);
    if (newline == std::string::npos)
      throw std::runtime_error("mixfix AST span exceeds source lines");
    offset = newline + 1;
  }
  offset += static_cast<std::size_t>(column - 1);
  if (offset > source.size())
    throw std::runtime_error("mixfix AST span exceeds source columns");
  return offset;
}

std::vector<SentencePieceId> spanIds(const IntegerTokenList &tokens,
                                     const SourceSpan &span) {
  const auto first =
      byteOffset(tokens.source(), span.startLine, span.startColumn);
  const auto last = byteOffset(tokens.source(), span.endLine, span.endColumn);
  std::vector<SentencePieceId> result;
  for (const auto &entry : tokens.entries()) {
    if (entry.id < 0)
      throw std::runtime_error("SentencePiece produced a negative token ID");
    if (entry.end > first && entry.begin < last) {
      result.push_back(static_cast<SentencePieceId>(entry.id));
    }
  }
  if (result.empty())
    throw std::runtime_error("mixfix AST span has no SentencePiece IDs");
  return result;
}

MixfixVocabularyId token(const MixfixContext &context, MixfixIrTokenKind kind,
                         IrWord value) {
  const auto found = std::find_if(
      context.outputVocabulary.begin(), context.outputVocabulary.end(),
      [&](const MixfixIrToken &item) {
        return item.kind == kind && item.value == value;
      });
  if (found == context.outputVocabulary.end()) {
    throw std::runtime_error(
        "mixfix IR cannot be represented by the fixed vocabulary (kind=" +
        std::to_string(static_cast<unsigned>(kind)) +
        ", value=" + std::to_string(value) + ")");
  }
  return static_cast<MixfixVocabularyId>(
      std::distance(context.outputVocabulary.begin(), found));
}

std::vector<MixfixVocabularyId> encodeTeacher(const FelidaeIr &ir) {
  const auto context = makeMixfixContext(ir);
  std::vector<MixfixVocabularyId> result;
  result.push_back(token(context, MixfixIrTokenKind::Accept, 0));
  const auto raw = [&](IrWord value) {
    result.push_back(token(context, MixfixIrTokenKind::Register, value));
  };
  const auto reg = raw;
  const auto constant = [&](IrWord value) {
    result.push_back(
        token(context, MixfixIrTokenKind::ConstantReference, value));
  };
  const auto symbol = [&](IrWord value) {
    result.push_back(token(context, MixfixIrTokenKind::SymbolReference, value));
  };
  for (std::size_t pc = 0; pc < ir.words.size();) {
    const auto opcode = static_cast<IrOpcode>(ir.words.at(pc));
    const auto require = [&](std::size_t width) {
      if (width > ir.words.size() - pc)
        throw std::runtime_error(
            "deterministic mixfix teacher IR is truncated");
    };
    if (opcode == IrOpcode::End) {
      result.push_back(token(context, MixfixIrTokenKind::End, 0));
      ++pc;
      continue;
    }
    result.push_back(
        token(context, MixfixIrTokenKind::Opcode, static_cast<IrWord>(opcode)));
    switch (opcode) {
    case IrOpcode::End:
      break;
    case IrOpcode::Jump:
      require(2);
      raw(ir.words[pc + 1]);
      pc += 2;
      break;
    case IrOpcode::LoadConst:
      require(3);
      reg(ir.words[pc + 1]);
      constant(ir.words[pc + 2]);
      pc += 3;
      break;
    case IrOpcode::LoadSymbol:
    case IrOpcode::StoreSymbol:
    case IrOpcode::MakeFact:
      require(3);
      reg(ir.words[pc + 1]);
      symbol(ir.words[pc + 2]);
      pc += 3;
      break;
    case IrOpcode::Move:
    case IrOpcode::JumpIfFalse:
    case IrOpcode::Return:
      require(3);
      reg(ir.words[pc + 1]);
      raw(ir.words[pc + 2]);
      pc += 3;
      break;
    case IrOpcode::Add:
    case IrOpcode::Sub:
    case IrOpcode::Mul:
    case IrOpcode::Div:
    case IrOpcode::Mod:
    case IrOpcode::GetField:
    case IrOpcode::SetField:
    case IrOpcode::Similarity:
    case IrOpcode::ForEachFact:
    case IrOpcode::HierarchyIsA:
    case IrOpcode::HierarchyCommonAncestors:
    case IrOpcode::HierarchyLeastCommonAncestors:
    case IrOpcode::HierarchyMostGeneralAncestors:
      require(4);
      reg(ir.words[pc + 1]);
      raw(ir.words[pc + 2]);
      raw(ir.words[pc + 3]);
      pc += 4;
      break;
    case IrOpcode::TemporalRank:
      require(4);
      reg(ir.words[pc + 1]);
      symbol(ir.words[pc + 2]);
      symbol(ir.words[pc + 3]);
      pc += 4;
      break;
    case IrOpcode::Compare:
      require(5);
      reg(ir.words[pc + 1]);
      raw(ir.words[pc + 2]);
      raw(ir.words[pc + 3]);
      raw(ir.words[pc + 4]);
      pc += 5;
      break;
    case IrOpcode::Membership:
      require(6);
      for (std::size_t index = 1; index < 6; ++index)
        raw(ir.words[pc + index]);
      pc += 6;
      break;
    case IrOpcode::Call:
    case IrOpcode::Builtin:
    case IrOpcode::SemanticEval:
    case IrOpcode::Numeric:
    case IrOpcode::Tensor:
    case IrOpcode::MakeArray: {
      require(4);
      reg(ir.words[pc + 1]);
      if (opcode == IrOpcode::Call)
        symbol(ir.words[pc + 2]);
      else
        raw(ir.words[pc + 2]);
      const auto count = ir.words[pc + 3];
      raw(count);
      require(4 + count);
      for (std::size_t index = 0; index < count; ++index) {
        raw(ir.words[pc + 4 + index]);
      }
      pc += 4 + count;
      break;
    }
    case IrOpcode::CallNamed:
    case IrOpcode::MakeMap: {
      require(4);
      reg(ir.words[pc + 1]);
      if (opcode == IrOpcode::CallNamed)
        symbol(ir.words[pc + 2]);
      else
        raw(ir.words[pc + 2]);
      const auto count = ir.words[pc + 3];
      raw(count);
      require(4 + count * 2);
      for (std::size_t index = 0; index < count; ++index) {
        symbol(ir.words[pc + 4 + index * 2]);
        raw(ir.words[pc + 5 + index * 2]);
      }
      pc += 4 + count * 2;
      break;
    }
    case IrOpcode::Count:
      throw std::runtime_error(
          "deterministic mixfix teacher has invalid opcode");
    }
  }
  if (resolveMixfixVocabularyIds(result, context) != ir.words) {
    throw std::runtime_error("mixfix teacher vocabulary round trip failed");
  }
  return result;
}

void visitExpr(const std::shared_ptr<Expr> &expression,
               std::vector<std::shared_ptr<OperatorExpression>> &output) {
  if (!expression)
    return;
  if (const auto operation =
          std::dynamic_pointer_cast<OperatorExpression>(expression)) {
    if (operation->coreOperator == CoreOperator::Unknown)
      output.push_back(operation);
    for (std::size_t index = 0; index < operation->captureCount(); ++index)
      visitExpr(operation->capture(index), output);
  } else if (const auto array =
                 std::dynamic_pointer_cast<ArrayExpr>(expression)) {
    for (const auto &item : array->items)
      visitExpr(item, output);
  } else if (const auto map = std::dynamic_pointer_cast<MapExpr>(expression)) {
    for (const auto &entry : map->entries)
      visitExpr(entry.value, output);
  } else if (const auto access =
                 std::dynamic_pointer_cast<AccessExpr>(expression)) {
    visitExpr(access->target, output);
  } else if (const auto term =
                 std::dynamic_pointer_cast<TermExpr>(expression)) {
    for (const auto &argument : term->args)
      visitExpr(argument.value, output);
  }
}

void visitCall(const Call &call,
               std::vector<std::shared_ptr<OperatorExpression>> &output) {
  for (const auto &argument : call.args)
    visitExpr(argument.value, output);
}

void visitGoal(const std::shared_ptr<Goal> &goal,
               std::vector<std::shared_ptr<OperatorExpression>> &output) {
  if (const auto call = std::dynamic_pointer_cast<CallGoal>(goal))
    visitCall(call->call, output);
  else if (const auto binary = std::dynamic_pointer_cast<BinaryGoal>(goal)) {
    visitExpr(binary->left, output);
    visitExpr(binary->right, output);
  } else if (const auto assign = std::dynamic_pointer_cast<AssignGoal>(goal))
    visitExpr(assign->expr, output);
  else if (const auto assign = std::dynamic_pointer_cast<MultiAssignGoal>(goal))
    visitExpr(assign->expr, output);
  else if (const auto where = std::dynamic_pointer_cast<WhereGoal>(goal))
    visitGoal(where->condition, output);
  else if (const auto conditional = std::dynamic_pointer_cast<IfGoal>(goal)) {
    visitGoal(conditional->condition, output);
    for (const auto &item : conditional->thenBranch)
      visitGoal(item, output);
    for (const auto &item : conditional->elseBranch)
      visitGoal(item, output);
  } else if (const auto returned =
                 std::dynamic_pointer_cast<ReturnGoal>(goal)) {
    for (const auto &field : returned->fields)
      visitExpr(field.value, output);
  } else if (const auto group = std::dynamic_pointer_cast<GroupGoal>(goal)) {
    for (const auto &item : group->goals)
      visitGoal(item, output);
  } else if (const auto alternatives =
                 std::dynamic_pointer_cast<OrGoal>(goal)) {
    for (const auto &branch : alternatives->branches)
      for (const auto &item : branch)
        visitGoal(item, output);
  }
}

bool isInvalidCase(const std::filesystem::path &path) {
  return std::any_of(path.begin(), path.end(), [](const auto &component) {
    return component == "invalid";
  });
}

std::vector<std::filesystem::path>
sources(const std::vector<std::filesystem::path> &roots, bool invalidCases) {
  std::vector<std::filesystem::path> result;
  for (const auto &root : roots) {
    if (std::filesystem::is_regular_file(root) && root.extension() == ".fx" &&
        isInvalidCase(root) == invalidCases)
      result.push_back(root);
    else if (std::filesystem::is_directory(root)) {
      for (const auto &entry :
           std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".fx" &&
            isInvalidCase(entry.path()) == invalidCases)
          result.push_back(entry.path());
      }
    } else
      throw std::runtime_error(
          "mixfix dataset input is not a .fx file or directory: " +
          root.string());
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<SentencePieceId> sourceIds(const IntegerTokenList &tokens) {
  std::vector<SentencePieceId> result;
  result.reserve(tokens.entries().size());
  for (const auto &entry : tokens.entries()) {
    if (entry.id < 0)
      throw std::runtime_error("SentencePiece produced a negative token ID");
    result.push_back(static_cast<SentencePieceId>(entry.id));
  }
  if (result.empty())
    throw std::runtime_error("source has no SentencePiece IDs");
  return result;
}

std::vector<std::pair<std::string, std::string>> generatedValidSources() {
  constexpr std::string_view transform =
      R"(@overload(operator: transformWith, pattern: "{value} transform {model} with {count}", type: mixfix, captures: {value: number, model: number, count: number}, result: number, precedence: relationship, associativity: none, cardinality: one, effects: pure, visibility: private)
transformValue() =>
    return value + model * count

main() =>
    return )";
  constexpr std::string_view choose =
      R"(@overload(operator: chooseOtherwise, pattern: "choose {left} otherwise {right}", type: mixfix, captures: {left: number, right: number}, result: number, precedence: relationship, associativity: right, cardinality: one, effects: pure, visibility: private)
chooseLarger() =>
    if left > right then
        return left
    else
        return right

main() =>
    return )";
  constexpr std::string_view plan =
      R"(@mixfix(pattern: "plan {value: number} using {enabled: bool}")
numberPlan(value:number, enabled:bool) =>
    return Plan(value: value, enabled: enabled)

main() =>
    return )";
  constexpr std::string_view wide =
      R"(@mixfix(pattern: "weigh {a: number} then {b: number} then {c: number} then {d: number} then {e: number} then {f: number}")
weighSix(a:number, b:number, c:number, d:number, e:number, f:number) =>
    return a + b + c + d + e + f

main() =>
    return )";
  constexpr std::string_view typed =
      R"(@mixfix(pattern: "classify {value: number} using {enabled: bool}")
classifyNumber(value:number, enabled:bool) =>
    return value

@mixfix(pattern: "classify {value: string} using {enabled: bool}")
classifyText(value:string, enabled:bool) =>
    return value

candidate := 7

main() =>
    return )";
  std::vector<std::pair<std::string, std::string>> result;
  result.reserve(120);
  for (std::size_t seed = 1; seed <= 24; ++seed) {
    const auto left = std::to_string(seed);
    const auto model = std::to_string(seed + 2);
    const auto count = std::to_string(seed % 7 + 1);
    result.emplace_back("generated-transform-" + std::to_string(seed),
                        std::string(transform) + left + " transform " + model +
                            " with " + count + "\n");
  }
  for (std::size_t seed = 1; seed <= 24; ++seed) {
    const auto left = std::to_string(seed);
    const auto middle = std::to_string(seed + 3);
    const auto right = std::to_string(seed + 6);
    result.emplace_back("generated-choose-" + std::to_string(seed),
                        std::string(choose) + "choose " + left +
                            " otherwise choose " + middle + " otherwise " +
                            right + "\n");
  }
  for (std::size_t seed = 1; seed <= 24; ++seed) {
    result.emplace_back("generated-plan-" + std::to_string(seed),
                        std::string(plan) + "plan " + std::to_string(seed * 3) +
                            " using " + (seed % 2 == 0 ? "true\n" : "false\n"));
  }
  for (std::size_t seed = 1; seed <= 16; ++seed) {
    std::string expression = std::to_string(seed + 7);
    for (std::size_t depth = 0; depth < 12; ++depth) {
      expression =
          "choose " + std::to_string(seed + depth) + " otherwise " + expression;
    }
    result.emplace_back("generated-deep-choose-" + std::to_string(seed),
                        std::string(choose) + expression + "\n");
  }
  for (std::size_t seed = 1; seed <= 16; ++seed) {
    std::string expression = "weigh";
    for (std::size_t index = 0; index < 6; ++index) {
      expression += " " + std::to_string(seed + index);
      if (index + 1 < 6)
        expression += " then";
    }
    result.emplace_back("generated-wide-repeated-anchor-" +
                            std::to_string(seed),
                        std::string(wide) + expression + "\n");
  }
  for (std::size_t seed = 1; seed <= 16; ++seed) {
    // A variable has no concrete capture type during parsing, so both typed
    // overloads remain structurally possible. These are safe-target
    // abstention teachers, not fabricated calls to either implementation.
    result.emplace_back("generated-typed-abstain-" + std::to_string(seed),
                        std::string(typed) + "classify candidate using " +
                            (seed % 2 == 0 ? "true\n" : "false\n"));
  }
  return result;
}

void writeJsonl(const std::filesystem::path &output,
                const std::vector<nlohmann::json> &records) {
  if (output.empty() || output.extension() != ".jsonl")
    throw std::runtime_error("dataset output must be .jsonl");
  if (records.empty())
    throw std::runtime_error("dataset has no records");
  if (!output.parent_path().empty())
    std::filesystem::create_directories(output.parent_path());
  const auto temporary = output.string() + ".tmp";
  std::ofstream stream(temporary, std::ios::trunc);
  if (!stream)
    throw std::runtime_error("cannot write dataset");
  for (const auto &record : records)
    stream << record.dump() << '\n';
  stream.close();
  if (!stream)
    throw std::runtime_error("cannot finish dataset");
  std::filesystem::rename(temporary, output);
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 3)
      throw std::runtime_error(
          "usage: felidae_extract_mixfix_dataset output.jsonl examples "
          "v2_examples [--rejections invalid.jsonl]");
    const std::filesystem::path output(argv[1]);
    if (output.extension() != ".jsonl")
      throw std::runtime_error("mixfix dataset output must be .jsonl");
    std::vector<std::filesystem::path> roots;
    std::optional<std::filesystem::path> rejectionOutput;
    for (int index = 2; index < argc; ++index) {
      if (std::string_view(argv[index]) == "--rejections") {
        if (rejectionOutput || ++index >= argc)
          throw std::runtime_error(
              "--rejections requires one output.jsonl path");
        rejectionOutput.emplace(argv[index]);
      } else {
        roots.emplace_back(argv[index]);
      }
    }
    if (roots.empty())
      throw std::runtime_error(
          "mixfix dataset requires at least one example root");
    std::vector<nlohmann::json> records;
    const auto sentencePieceIdentity = felidaeSentencePieceModelIdentity();
    std::set<std::pair<std::vector<SentencePieceId>,
                       std::vector<MixfixVocabularyId>>>
        unique;
    std::size_t boundedOut = 0;
    std::size_t rejectedSources = 0;
    std::vector<std::pair<std::string, std::string>> teachers;
    for (const auto &path : sources(roots, false))
      teachers.emplace_back(path.string(), readSourceFile(path));
    auto generated = generatedValidSources();
    teachers.insert(teachers.end(), std::make_move_iterator(generated.begin()),
                    std::make_move_iterator(generated.end()));
    for (const auto &[label, source] : teachers) {
      Program program;
      try {
        program = parseProgramText(source);
      } catch (const std::exception &error) {
        ++rejectedSources;
        std::cerr << "skipping non-teacher source " << label << ": "
                  << error.what() << '\n';
        continue;
      }
      const IntegerTokenList tokens(felidaeSentencePieceModel(), source);
      std::vector<std::shared_ptr<OperatorExpression>> operations;
      for (const auto &global : program.globals)
        visitExpr(global->expr, operations);
      for (const auto &clause : program.clauses) {
        visitCall(clause->head, operations);
        for (const auto &annotation : clause->annotations)
          visitCall(annotation, operations);
        for (const auto &goal : clause->body)
          visitGoal(goal, operations);
        for (const auto &branch : clause->fallbackBranches)
          for (const auto &goal : branch)
            visitGoal(goal, operations);
      }
      if (!operations.empty())
        std::cerr << "found " << operations.size()
                  << " custom mixfix expressions in " << label << '\n';
      for (const auto &operation : operations) {
        const auto input = spanIds(tokens, operation->sourceSpan);
        try {
          const auto target =
              encodeTeacher(IrCodeGenerator::lowerExpression(operation));
          if (unique.emplace(input, target).second)
            records.push_back(
                {{"schema_version", 3},
                 {"sentencepiece_model_identity", sentencePieceIdentity},
                 {"compiler_ir_vocabulary", kMixfixIrVocabularyVersion},
                 {"input_ids", input},
                 {"target_ids", target},
                 {"decision", "ACCEPT"}});
        } catch (const std::runtime_error &error) {
          const std::string_view message(error.what());
          if (message.find("cannot be represented by the fixed vocabulary") !=
              std::string_view::npos) {
            ++boundedOut;
            std::cerr << "skipping bounded-out mixfix teacher in " << label
                      << ": " << message << '\n';
          } else if (message.find("requires verified model target selection") !=
                         std::string_view::npos ||
                     message.find("has no direct IR lowering") !=
                         std::string_view::npos) {
            const auto context = makeMixfixContext(FelidaeIr{});
            const std::vector<MixfixVocabularyId> target{
                token(context, MixfixIrTokenKind::Abstain, 0)};
            if (unique.emplace(input, target).second) {
              records.push_back(
                  {{"schema_version", 3},
                   {"sentencepiece_model_identity", sentencePieceIdentity},
                   {"compiler_ir_vocabulary", kMixfixIrVocabularyVersion},
                   {"input_ids", input},
                   {"target_ids", target},
                   {"decision", "ABSTAIN"}});
            }
            std::cerr << "recording ABSTAIN mixfix teacher in " << label << ": "
                      << message << '\n';
          } else {
            throw;
          }
        }
      }
    }
    if (records.empty())
      throw std::runtime_error(
          "no deterministically resolved mixfix expressions were found");
    std::size_t rejectionRecords = 0;
    std::size_t acceptedInvalidSources = 0;
    std::vector<nlohmann::json> rejections;
    if (rejectionOutput) {
      for (const auto &path : sources(roots, true)) {
        const auto source = readSourceFile(path);
        const IntegerTokenList tokens(felidaeSentencePieceModel(), source);
        std::uint32_t stage = 0;
        try {
          const auto program = parseProgramText(source);
          try {
            auto module = IrCodeGenerator{}.compile(program);
            try {
              FelidaeKnowledgeRuntime runtime;
              RegisterVm vm;
              (void)vm.executeMain(verifyIrModule(std::move(module)), runtime);
            } catch (const std::exception &) {
              stage = 3; // Verified VM/runtime rejection.
            }
          } catch (const std::exception &) {
            stage = 2; // AST-to-IR/compiler rejection.
          }
        } catch (const std::exception &) {
          stage = 1; // SentencePiece-aware parser rejection.
        }
        if (stage == 0) {
          // Fixture directories occasionally retain a case whose
          // historical resource limit no longer applies. It is not
          // a valid negative label, so report and exclude it.
          ++acceptedInvalidSources;
          std::cerr
              << "excluding stale invalid fixture that executed successfully: "
              << path.string() << '\n';
          continue;
        }
        const auto context = makeMixfixContext(FelidaeIr{});
        const auto reject = token(context, MixfixIrTokenKind::Reject, 0);
        rejections.push_back(
            {{"schema_version", 3},
             {"sentencepiece_model_identity", sentencePieceIdentity},
             {"compiler_ir_vocabulary", kMixfixIrVocabularyVersion},
             {"input_ids", sourceIds(tokens)},
             {"target_ids", std::vector<MixfixVocabularyId>{reject}},
             {"decision", "REJECT"},
             {"rejection_stage", stage}});
      }
      rejectionRecords = rejections.size();
    }
    // All valid and invalid sources are checked before either reusable
    // corpus is replaced, so a failed input cannot leave a mixed run.
    writeJsonl(output, records);
    if (rejectionOutput)
      writeJsonl(*rejectionOutput, rejections);
    const auto abstentionRecords = static_cast<std::size_t>(std::count_if(
        records.begin(), records.end(), [](const nlohmann::json &record) {
          return record.value("decision", std::string{}) == "ABSTAIN";
        }));
    const auto acceptanceRecords = records.size() - abstentionRecords;
    std::cout << records.size() << " compiler mixfix records written to "
              << output.string() << " (ACCEPT=" << acceptanceRecords
              << ", ABSTAIN=" << abstentionRecords << ")"
              << "; " << boundedOut
              << " expressions exceeded fixed structural bounds; "
              << rejectedSources
              << " valid-root sources were rejected by the current parser; "
              << rejectionRecords
              << " invalid rejection-evaluation records written; "
              << acceptedInvalidSources << " stale invalid fixtures excluded\n";
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
