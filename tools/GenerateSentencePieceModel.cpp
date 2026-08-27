#include "FelidaeGrammar.h"

#include <sentencepiece_model.pb.h>
#include <sentencepiece_processor.h>
#include <sentencepiece_trainer.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct TokenizerCorpus {
    std::vector<std::string> sources;
};

TokenizerCorpus loadCorpus(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("unable to open tokenizer JSONL dataset " + path);
    TokenizerCorpus corpus;
    std::set<std::string> ids;
    std::set<std::string> families;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty()) continue;
        const auto record = nlohmann::json::parse(line);
        if (!record.is_object() || record.size() != 4 ||
            !record.contains("schema_version") || !record.at("schema_version").is_number_unsigned() ||
            record.at("schema_version").get<std::uint64_t>() != 1 ||
            !record.contains("id") || !record.at("id").is_string() ||
            !record.contains("family") || !record.at("family").is_string() ||
            !record.contains("source") || !record.at("source").is_string()) {
            throw std::runtime_error("invalid tokenizer JSONL record at line " + std::to_string(lineNumber));
        }
        const auto id = record.at("id").get<std::string>();
        const auto family = record.at("family").get<std::string>();
        const auto source = record.at("source").get<std::string>();
        if (id.empty() || id.size() > 128 || family.empty() || family.size() > 64 ||
            source.empty() || source.size() > 16384 || !ids.insert(id).second) {
            throw std::runtime_error("invalid tokenizer JSONL values at line " + std::to_string(lineNumber));
        }
        families.insert(family);
        corpus.sources.push_back(source);
    }
    if (!input.eof()) throw std::runtime_error("unable to read tokenizer JSONL dataset");
    if (corpus.sources.size() < 20 || families.size() < 10) {
        throw std::runtime_error("tokenizer JSONL dataset needs at least 20 records across 10 syntax families");
    }
    return corpus;
}

class CorpusIterator final : public sentencepiece::SentenceIterator {
public:
    explicit CorpusIterator(const std::vector<std::string>& corpus) : corpus_(corpus) {}
    bool done() const override { return index_ >= corpus_.size(); }
    void Next() override { if (!done()) ++index_; }
    const std::string& value() const override { return corpus_[index_]; }
    absl::Status status() const override { return absl::OkStatus(); }
private:
    const std::vector<std::string>& corpus_;
    std::size_t index_ = 0;
};

void writeFile(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(data.data(), static_cast<std::streamsize>(data.size()))) {
        throw std::runtime_error("unable to write " + path);
    }
}

bool beginsWithAsciiUppercase(const std::string& piece) {
    // A SentencePiece can contain the complete leading identifier fragment
    // ("Stage"), only a prefix ("St"), or one byte ("S").  The grammar
    // property is carried by its first source character, not by a particular
    // fragmentation length.
    std::size_t start = 0;
    // SentencePiece represents preceding source whitespace with U+2581. The
    // marker is not part of the identifier, so it must not hide an uppercase
    // first source byte after indentation or a separator.
    if (piece.size() >= 3 && piece.compare(0, 3, "\xE2\x96\x81") == 0) start = 3;
    if (piece.size() > start && piece[start] >= 'A' && piece[start] <= 'Z') return true;
    // Byte-fallback pieces are serialized by SentencePiece as <0xNN>.
    return piece.size() == 6 && piece[0] == '<' && piece[1] == '0' && piece[2] == 'x' &&
           piece[5] == '>' &&
           ((piece[3] == '4' && piece[4] >= '1' && piece[4] <= '9') ||
            (piece[3] == '5' && piece[4] >= '0' && piece[4] <= 'A'));
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: GenerateSentencePieceModel MODEL_PATH IDS_HEADER_PATH DATASET_JSONL\n";
        return 2;
    }
    try {
        const auto dataset = loadCorpus(argv[3]);
        std::vector<std::string> corpus;
        corpus.reserve(std::size(Felidae::kBuiltinTokens) + dataset.sources.size());
        for (const auto& token : Felidae::kBuiltinTokens) corpus.emplace_back(token.spelling);
        corpus.insert(corpus.end(), dataset.sources.begin(), dataset.sources.end());
        CorpusIterator iterator(corpus);
        sentencepiece::TrainerComponents components;
        components.sentence_iterator = &iterator;
        auto* spec = components.mutable_trainer_spec();
        spec->set_model_type(sentencepiece::TrainerSpec::BPE);
        spec->set_vocab_size(1024);
        spec->set_hard_vocab_limit(false);
        spec->set_character_coverage(1.0F);
        spec->set_shuffle_input_sentence(false);
        spec->set_num_threads(1);
        spec->set_unk_id(0);
        // Every valid UTF-8 source byte must have an integer representation.
        // Without byte fallback, unrelated user-defined identifiers can both
        // become <unk> and no integer parser can distinguish their sequences.
        spec->set_byte_fallback(true);
        spec->set_bos_id(-1);
        spec->set_eos_id(-1);
        spec->set_pad_id(-1);
        for (const auto& token : Felidae::kBuiltinTokens) {
            spec->add_user_defined_symbols(std::string(token.spelling));
        }
        auto* normalizer = components.mutable_normalizer_spec();
        normalizer->set_name("identity");
        normalizer->set_add_dummy_prefix(false);
        normalizer->set_remove_extra_whitespaces(false);
        normalizer->set_escape_whitespaces(true);

        sentencepiece::SetMinLogLevel(2);
        std::string serialized;
        const auto status = sentencepiece::SentencePieceTrainer::Train(components, &serialized);
        if (!status.ok()) throw std::runtime_error(status.ToString());
        sentencepiece::ModelProto model;
        if (!model.ParseFromString(serialized)) throw std::runtime_error("model serialization failed");

        std::string ids = "// Generated by tools/GenerateSentencePieceModel.cpp. Do not edit.\n#pragma once\n\n";
        ids += "#include <cstdint>\n\nnamespace Felidae {\n";
        ids += "inline constexpr std::uint32_t kFelidaeTokenizerDatasetSchemaVersion = 1;\n";
        ids += "inline constexpr std::uint32_t kFelidaeTokenizerDatasetRecordCount = " +
               std::to_string(dataset.sources.size()) + ";\n";
        ids += "inline constexpr std::uint32_t kFelidaeSentencePieceVocabularySize = " +
               std::to_string(model.pieces_size()) + ";\n";
        ids += "inline constexpr int kFelidaeBuiltinSentencePieceIds[] = {\n";
        for (const auto& token : Felidae::kBuiltinTokens) {
            int id = -1;
            for (int index = 0; index < model.pieces_size(); ++index) {
                if (model.pieces(index).piece() == token.spelling) { id = index; break; }
            }
            if (id < 0) throw std::runtime_error("missing built-in " + std::string(token.spelling));
            // idName is source-safe even when a grammar symbol itself is a
            // line-comment escape such as a backslash.
            ids += "    " + std::to_string(id) + ", // " + std::string(token.idName) + "\n";
        }
        ids += "};\n\nnamespace TokenId {\nusing Id = std::int32_t;\n";
        ids += "constexpr Id UNKNOWN = 0;\n";
        for (const auto& token : Felidae::kBuiltinTokens) {
            int id = -1;
            for (int index = 0; index < model.pieces_size(); ++index) {
                if (model.pieces(index).piece() == token.spelling) { id = index; break; }
            }
            ids += "constexpr Id " + std::string(token.idName) + " = " + std::to_string(id) + ";\n";
        }
        ids += "} // namespace TokenId\n\ninline constexpr bool isCapitalizedIdentifierStartId(TokenId::Id id) {\n";
        ids += "    switch (id) {\n";
        for (int index = 0; index < model.pieces_size(); ++index) {
            if (beginsWithAsciiUppercase(model.pieces(index).piece())) {
                ids += "        case " + std::to_string(index) + ": return true;\n";
            }
        }
        ids += "        default: return false;\n    }\n}\n";
        ids += "} // namespace Felidae\n";
        writeFile(argv[1], serialized);
        writeFile(argv[2], ids);
    } catch (const std::exception& error) {
        std::cerr << "felidae SentencePiece model generation failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
