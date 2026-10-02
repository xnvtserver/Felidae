#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace Felidae {

class TerminalUi;
class WordVocabulary;

class ReplLineEditor {
public:
    ReplLineEditor(TerminalUi& ui, std::istream& input,
                   std::shared_ptr<WordVocabulary> highlightingTokenizer = {});
    bool readLine(bool continuation, std::string& line);

private:
    TerminalUi& ui_;
    std::istream& input_;
    std::shared_ptr<WordVocabulary> tokenizer_;
    std::vector<std::string> history_;
};

} // namespace Felidae
