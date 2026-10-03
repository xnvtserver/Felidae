#include "TerminalUi.h"

#include "Environment.h"

#include <algorithm>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <iterator>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <io.h>
#include <windows.h>
#ifdef IN
#undef IN
#endif
#ifdef TRUE
#undef TRUE
#endif
#ifdef FALSE
#undef FALSE
#endif
#ifdef THIS
#undef THIS
#endif
#else
#include <unistd.h>
#endif

namespace Felidae {
namespace {

bool isInteractiveTerminal(std::ostream& output) {
    if (&output != &std::cout) return false;
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

bool enableTerminalControl() {
    if (const auto term = environmentVariable("TERM"); term && *term == "dumb") {
        return false;
    }
#ifdef _WIN32
    const HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    if (console == INVALID_HANDLE_VALUE || console == nullptr) return false;
    DWORD mode = 0;
    if (!GetConsoleMode(console, &mode)) return false;
    return SetConsoleMode(console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return true;
#endif
}

bool terminalColorAllowed() {
    return !environmentVariable("NO_COLOR").has_value();
}

bool isKeyword(TokenId::Id id) {
    switch (id) {
        case TokenId::IMPORT: case TokenId::NOT: case TokenId::AND:
        case TokenId::OR: case TokenId::THEN: case TokenId::AS:
        case TokenId::IF: case TokenId::ELSE: case TokenId::RETURN:
        case TokenId::WHERE: case TokenId::EXTEND: case TokenId::LAMBDA:
        case TokenId::CLASS: case TokenId::END: case TokenId::INDEX:
        case TokenId::EXTENDS: case TokenId::ELIF: case TokenId::NEW:
        case TokenId::FOR: case TokenId::IN: case TokenId::WHILE:
        case TokenId::SWITCH: case TokenId::CASE: case TokenId::DEFAULT:
        case TokenId::BREAK: case TokenId::CONTINUE: case TokenId::DEF:
        case TokenId::THIS: case TokenId::SUPER:
        case TokenId::TRY: case TokenId::CATCH:
            return true;
        default: return false;
    }
}

bool isOperatorOrPunctuation(TokenId::Id id) {
    return id >= TokenId::LPAREN && id <= TokenId::GREATER_EQUAL;
}

std::size_t displayedCharacters(std::string_view text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0U) != 0x80U) ++count;
    }
    return count;
}

} // namespace

class TerminalUi::Activity {
public:
    Activity(std::ostream& output, std::string label)
        : output_(output), worker_([this] { animate(); }) { (void)label; }

    ~Activity() {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
        }
        changed_.notify_all();
        if (worker_.joinable()) worker_.join();
        if (shown_) output_ << "\r   \r" << std::flush;
    }

    Activity(const Activity&) = delete;
    Activity& operator=(const Activity&) = delete;

private:
    void animate() {
        std::unique_lock lock(mutex_);
        if (changed_.wait_for(lock, std::chrono::milliseconds(180),
                              [this] { return stopped_.load(); })) return;
        static constexpr const char* frames[] = {
            "#  ",
            "## ",
            "###"
        };
        std::size_t frame = 0;
        while (!stopped_) {
            shown_ = true;
            output_ << '\r' << frames[frame++ % std::size(frames)] << std::flush;
            changed_.wait_for(lock, std::chrono::milliseconds(90),
                              [this] { return stopped_.load(); });
        }
    }

    std::ostream& output_;
    std::atomic_bool stopped_{false};
    std::atomic_bool shown_{false};
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
};

TerminalUi::TerminalUi(std::ostream& output)
    : output_(output),
      interactive_(isInteractiveTerminal(output) && enableTerminalControl()),
      color_(interactive_ && terminalColorAllowed()) {}

void TerminalUi::styled(std::string_view style, std::string_view text) {
    if (color_) output_ << "\x1b[" << style << 'm';
    output_ << text;
    if (color_) output_ << "\x1b[0m";
}

void TerminalUi::banner(std::string_view name, std::string_view version,
                        std::string_view description) {
    const std::string title = std::string(name) + " REPL  v" + std::string(version);
    styled("1;96", "+------------------------------------------------------------+\n");
    styled("1;96", "|  ");
    styled("1;97", title);
    if (title.size() < 57) output_ << std::string(57 - title.size(), ' ');
    styled("1;96", "|\n");
    styled("1;96", "+------------------------------------------------------------+\n");
    styled("2", description);
    output_ << '\n';
    styled("2", "Type :help for commands. def/class input closes with end.");
    output_ << "\n\n";
}

void TerminalUi::clearScreen() {
    if (interactive_) {
        output_ << "\x1b[2J\x1b[H" << std::flush;
    } else {
        output_ << "[screen cleared]\n";
    }
}

void TerminalUi::prompt(bool continuation) {
    if (!interactive_) return;
    if (continuation) {
        styled("2;36", "      ... ");
    } else {
        styled("1;96", "felidae");
        styled("1;37", " > ");
    }
    output_ << std::flush;
}

void TerminalUi::redrawInput(bool continuation, const IntegerTokenList& tokens,
                             std::size_t cursor) {
    output_ << "\r\x1b[2K";
    prompt(continuation);
    const auto& source = tokens.source();
    std::size_t written = 0;
    bool inString = false;
    bool escaped = false;
    bool inComment = false;
    for (const auto& entry : tokens.entries()) {
        if (entry.begin > written) output_ << source.substr(written, entry.begin - written);
        const std::string text = source.substr(entry.begin, entry.end - entry.begin);
        const auto id = entry.id;
        if (inComment || id == TokenId::COMMENT) {
            inComment = true;
            styled("2;37", text);
        } else if (inString || id == TokenId::QUOTE) {
            styled("32", text);
            if (!inString && id == TokenId::QUOTE) {
                inString = true;
                escaped = false;
            } else if (id == TokenId::BACKSLASH) {
                escaped = !escaped;
            } else {
                if (id == TokenId::QUOTE && !escaped) inString = false;
                escaped = false;
            }
        } else if (isKeyword(id)) {
            styled("1;35", text);
        } else if (id == TokenId::TRUE || id == TokenId::FALSE || id == TokenId::NIL ||
                   (id >= TokenId::DIGIT_0 && id <= TokenId::DIGIT_9)) {
            styled("33", text);
        } else if (isOperatorOrPunctuation(id) || id == TokenId::AT) {
            styled("1;36", text);
        } else if (isCapitalizedIdentifierStartId(id)) {
            styled("1;34", text);
        } else {
            output_ << text;
        }
        written = entry.end;
    }
    if (written < source.size()) output_ << source.substr(written);
    const std::size_t boundedCursor = std::min(cursor, source.size());
    const std::size_t moveLeft = displayedCharacters(
        std::string_view(source).substr(boundedCursor));
    if (moveLeft != 0) output_ << "\x1b[" << moveLeft << 'D';
    output_ << std::flush;
}

void TerminalUi::heading(std::string_view text) {
    styled("1;96", text);
    output_ << '\n';
}

void TerminalUi::command(std::string_view syntax, std::string_view description) {
    output_ << "  ";
    styled("1;93", syntax);
    if (syntax.size() < 28) output_ << std::string(28 - syntax.size(), ' ');
    output_ << description << '\n';
}

void TerminalUi::note(std::string_view text) {
    styled("2", text);
    output_ << '\n';
}

void TerminalUi::success(std::string_view text) {
    styled("1;32", "[ok] ");
    output_ << text << '\n';
}

void TerminalUi::warning(std::string_view text) {
    styled("1;33", "[warning] ");
    output_ << text << '\n';
}

void TerminalUi::error(std::string_view text) {
    styled("1;31", "[error] ");
    output_ << text << '\n';
}

void TerminalUi::result(std::string_view text) {
    styled("1;35", "=> ");
    output_ << text << '\n';
}

void TerminalUi::queryHeading() {
    styled("1;35", "[query results]");
    output_ << '\n';
}

void TerminalUi::debug(std::string_view text) {
    styled("1;34", "[debug] ");
    styled("36", text);
    output_ << '\n';
}

void TerminalUi::metric(std::string_view label, std::string_view value) {
    output_ << "  ";
    styled("36", label);
    if (label.size() < 28) output_ << std::string(28 - label.size(), ' ');
    styled("1;97", value);
    output_ << '\n';
}

void TerminalUi::codeBlock(std::string_view source) {
    std::istringstream lines{std::string(source)};
    std::string line;
    std::size_t number = 1;
    while (std::getline(lines, line)) {
        styled("2;36", std::to_string(number++) + " | ");
        styled("36", line);
        output_ << '\n';
    }
}

std::shared_ptr<TerminalUi::Activity> TerminalUi::activity(std::string_view label) {
    if (!interactive_) return {};
    return std::make_shared<Activity>(output_, std::string(label));
}

} // namespace Felidae
