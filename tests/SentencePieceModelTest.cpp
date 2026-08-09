#include "Parser.h"
#include "Token.h"

#include <sentencepiece_processor.h>
#include <sentencepiece.pb.h>

#include <cassert>
#include <iostream>

int main() {
    sentencepiece::SentencePieceProcessor model;
    const auto loaded = model.Load(FELIDAE_SENTENCEPIECE_MODEL_PATH);
    assert(loaded.ok());

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

    // Construction validates the model using the same native processor that
    // compiles parser mixfix anchors.
    Felidae::Lexer legacyCompatibilityScanner("");
    Felidae::Parser parser(legacyCompatibilityScanner);
    (void)parser;
    std::cout << "felidae SentencePiece model validation passed\n";
}
