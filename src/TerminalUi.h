#pragma once

#include "IntegerTokenList.h"

#include <iosfwd>
#include <memory>
#include <string_view>

namespace Felidae {

// Small ANSI terminal facade used by the interactive REPL and the help text.
// Redirected output remains plain text, and NO_COLOR disables styling explicitly.
class TerminalUi {
public:
    class Activity;
    explicit TerminalUi(std::ostream& output);

    void logo();
    void banner(std::string_view name, std::string_view version,
                std::string_view description);
    void clearScreen();
    void prompt(bool continuation);
    bool interactive() const noexcept { return interactive_; }
    void redrawInput(bool continuation, const IntegerTokenList& tokens,
                     std::size_t cursor);
    void heading(std::string_view text);
    void command(std::string_view syntax, std::string_view description);
    void note(std::string_view text);
    void success(std::string_view text);
    void warning(std::string_view text);
    void error(std::string_view text);
    void result(std::string_view text);
    void queryHeading();
    void debug(std::string_view text);
    void metric(std::string_view label, std::string_view value);
    void codeBlock(std::string_view source);
    std::shared_ptr<Activity> activity(std::string_view label);

private:
    std::ostream& output_;
    bool interactive_ = false;
    bool color_ = false;

    void styled(std::string_view style, std::string_view text);
};

} // namespace Felidae
