# notepad-plus-plus-extension

Notepad++ support for Felidae (`.fx`) files.

Notepad++ plugins normally require compiling a C++ DLL. Felidae has no
Notepad++-native parser and, like this repository's other lightweight
editors (Sublime, Vim, Emacs), doesn't need one — Notepad++'s built-in
**User Defined Language** (UDL) and **AutoComplete API** systems cover
syntax highlighting and keyword/function completion entirely through
importable XML, with no plugin build step.

## Features

- `Felidae.xml` — a UDL definition: keywords (`extend where if else return
  lambda then import`), constants (`nil true false`), type annotations,
  standard-library module names, `#` line comments, strings, numbers,
  operators (`=> := == != <= >=`), and `{ }` code folding. Associates
  itself with the `.fx` extension on import.
- `autoCompletion/Felidae.xml` — Notepad++'s AutoComplete API file,
  generated from the same `builtin-docs.json` used by `vs-code-extension`
  and `intellij-idea-extension`. Typing a builtin name (e.g. `math:add(`,
  `Fact:find(`) shows its parameters and description; language keywords
  and standard-library module names complete as plain words.

Not included: a function-list panel entry and a Run/Debug command. Those
require editing Notepad++'s shared `functionList/associationMap.xml` and
either the NppExec plugin or a custom shortcuts entry, both of which live
outside a user-local, drag-and-drop install. `NppExec` users can wire up
`felidae "$(FULL_CURRENT_PATH)"` / `felidae_debug "$(FULL_CURRENT_PATH)"`
/ `celidae "$(FULL_CURRENT_PATH)"` execute commands manually, matching
`felidae.interpreterPath` / `felidae.debugInterpreterPath` /
`felidae.celidaePath` in `vs-code-extension`.

## Installation

1. **Syntax highlighting** — in Notepad++, open `Language > Define your
   language… > Import…` and select `Felidae.xml` from this folder. Restart
   Notepad++. `.fx` files now open with the `Felidae` language selected
   automatically (the import registers the `fx` extension).
2. **Autocomplete** — copy `autoCompletion/Felidae.xml` to
   `%APPDATA%\Notepad++\autoCompletion\Felidae.xml` (paste that path into
   Explorer's address bar to get there directly). Restart Notepad++, then
   enable `Settings > Preferences… > Auto-Completion > Enable
   auto-completion on each input`. Function parameter hints work once a
   `.fx` file with the `Felidae` language is active.

Colors are unstyled defaults; adjust them under `Settings > Style
Configurator… > Language: Felidae` — the style names there (`KEYWORD1`,
`COMMENT`, `NUMBER`, …) match the ones set in `Felidae.xml`.

## Configuration

There is no per-project settings file — Notepad++ UDLs are user-global.
Re-running the import step updates the definition for all users on the
same Windows account; there's no separate per-workspace override.
