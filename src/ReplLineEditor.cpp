#include "ReplLineEditor.h"

#include "IntegerTokenList.h"
#include "TerminalUi.h"

#include <iostream>
#include <utility>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace Felidae {
namespace {

std::size_t previousCharacter(const std::string& text, std::size_t cursor) {
    if (cursor == 0) return 0;
    std::size_t previous = cursor - 1;
    while (previous > 0 &&
           (static_cast<unsigned char>(text[previous]) & 0xC0U) == 0x80U) {
        --previous;
    }
    return previous;
}

std::size_t nextCharacter(const std::string& text, std::size_t cursor) {
    if (cursor >= text.size()) return text.size();
    std::size_t next = cursor + 1;
    while (next < text.size() &&
           (static_cast<unsigned char>(text[next]) & 0xC0U) == 0x80U) {
        ++next;
    }
    return next;
}

#ifndef _WIN32
class RawTerminal {
public:
    RawTerminal() {
        if (tcgetattr(STDIN_FILENO, &original_) != 0) return;
        termios raw = original_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        active_ = tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0;
    }
    ~RawTerminal() {
        if (active_) tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
    }
    bool active() const noexcept { return active_; }
private:
    termios original_{};
    bool active_ = false;
};
#endif

enum class Key {
    Character, Enter, Interrupt, EndOfInput, Backspace, Delete,
    Left, Right, Up, Down, Home, End
};

struct KeyPress {
    Key key = Key::Character;
    char character = 0;
};

KeyPress readKey(std::istream& input) {
#ifdef _WIN32
    (void)input;
    const int value = _getch();
    if (value == 0 || value == 224) {
        switch (_getch()) {
            case 71: return {Key::Home};
            case 72: return {Key::Up};
            case 75: return {Key::Left};
            case 77: return {Key::Right};
            case 79: return {Key::End};
            case 80: return {Key::Down};
            case 83: return {Key::Delete};
            default: return {Key::Character, 0};
        }
    }
    if (value == '\r' || value == '\n') return {Key::Enter};
    if (value == 3) return {Key::Interrupt};
    if (value == 8 || value == 127) return {Key::Backspace};
    if (value == 4) return {Key::EndOfInput};
    return {Key::Character, static_cast<char>(value)};
#else
    const int value = input.get();
    if (value == EOF || value == 4) return {Key::EndOfInput};
    if (value == '\r' || value == '\n') return {Key::Enter};
    if (value == 3) return {Key::Interrupt};
    if (value == 8 || value == 127) return {Key::Backspace};
    if (value != 27) return {Key::Character, static_cast<char>(value)};
    const int bracket = input.get();
    if (bracket != '[') return {Key::Character, 0};
    const int code = input.get();
    switch (code) {
        case 'A': return {Key::Up};
        case 'B': return {Key::Down};
        case 'C': return {Key::Right};
        case 'D': return {Key::Left};
        case 'H': return {Key::Home};
        case 'F': return {Key::End};
        case '3':
            if (input.get() == '~') return {Key::Delete};
            break;
        default: break;
    }
    return {Key::Character, 0};
#endif
}

} // namespace

ReplLineEditor::ReplLineEditor(TerminalUi& ui, std::istream& input,
                               std::shared_ptr<ByteTokenizer> tokenizer)
    : ui_(ui), input_(input), tokenizer_(std::move(tokenizer)) {
    // Highlighting uses the production lexer with an isolated vocabulary.
    // Partial words typed and erased must not consume dynamic token IDs in the
    // interpreter's authoritative tokenizer.
    if (!tokenizer_) tokenizer_ = std::make_shared<ByteTokenizer>();
}

bool ReplLineEditor::readLine(bool continuation, std::string& line) {
    if (!ui_.interactive() || &input_ != &std::cin) {
        ui_.prompt(continuation);
        return static_cast<bool>(std::getline(input_, line));
    }
#ifndef _WIN32
    RawTerminal terminal;
    if (!terminal.active()) {
        ui_.prompt(continuation);
        return static_cast<bool>(std::getline(input_, line));
    }
#endif
    line.clear();
    std::size_t cursor = 0;
    std::size_t historyIndex = history_.size();
    std::string pending;
    const auto redraw = [&] {
        IntegerTokenList tokens(tokenizer_, line);
        ui_.redrawInput(continuation, tokens, cursor);
    };
    redraw();
    while (true) {
        const KeyPress pressed = readKey(input_);
        switch (pressed.key) {
            case Key::Enter:
                std::cout << '\n';
                if (!line.empty() && (history_.empty() || history_.back() != line)) {
                    history_.push_back(line);
                }
                return true;
            case Key::Interrupt:
                line.clear();
                std::cout << "^C\n";
                return true;
            case Key::EndOfInput:
                if (line.empty()) return false;
                std::cout << '\n';
                return true;
            case Key::Backspace:
                if (cursor != 0) {
                    const auto previous = previousCharacter(line, cursor);
                    line.erase(previous, cursor - previous);
                    cursor = previous;
                }
                break;
            case Key::Delete:
                if (cursor < line.size()) {
                    line.erase(cursor, nextCharacter(line, cursor) - cursor);
                }
                break;
            case Key::Left: cursor = previousCharacter(line, cursor); break;
            case Key::Right: cursor = nextCharacter(line, cursor); break;
            case Key::Home: cursor = 0; break;
            case Key::End: cursor = line.size(); break;
            case Key::Up:
                if (!history_.empty() && historyIndex > 0) {
                    if (historyIndex == history_.size()) pending = line;
                    line = history_[--historyIndex];
                    cursor = line.size();
                }
                break;
            case Key::Down:
                if (historyIndex < history_.size()) {
                    ++historyIndex;
                    line = historyIndex == history_.size() ? pending : history_[historyIndex];
                    cursor = line.size();
                }
                break;
            case Key::Character:
                if (pressed.character >= 32 || pressed.character == '\t') {
                    line.insert(cursor, 1, pressed.character);
                    ++cursor;
                }
                break;
        }
        redraw();
    }
}

} // namespace Felidae
