-- Neovim-specific setup: registers the Felidae tree-sitter grammar with
-- nvim-treesitter, if that plugin is installed, so `:TSInstall felidae`
-- builds tree-sitter-felidae/ (already in this repository, see its
-- README.md - "intentionally kept separate from the C++ parser") straight
-- from grammar.js. Highlighting, indentation, `.fx` file association, and
-- run/check/format commands all work without this module or nvim-treesitter
-- at all - see ../syntax/felidae.vim, ../indent/felidae.vim,
-- ../ftplugin/felidae.vim, and ../autoload/felidae.vim, which are plain
-- Vimscript and apply in both Vim and Neovim. This module only adds the
-- richer, tree-sitter-based highlighting path for users who opt in.

local M = {}

-- Directory this file lives in, e.g. ".../vim-extension/lua/felidae".
local function plugin_dir()
  local source = debug.getinfo(1, "S").source:sub(2)
  return vim.fn.fnamemodify(source, ":h:h:h")
end

-- ".../vim-extension/.." = the repository root containing tree-sitter-felidae/.
local function grammar_dir()
  return plugin_dir() .. "/../tree-sitter-felidae"
end

function M.setup(opts)
  opts = opts or {}

  local ok, parsers = pcall(require, "nvim-treesitter.parsers")
  if not ok then
    return false, "nvim-treesitter is not installed; traditional syntax/indent files are still active"
  end

  local grammar_path = opts.grammar_dir or grammar_dir()
  if vim.fn.isdirectory(grammar_path) == 0 then
    return false, "tree-sitter-felidae grammar not found at " .. grammar_path
  end

  local configs = parsers.get_parser_configs()
  configs.felidae = {
    install_info = {
      url = grammar_path,
      files = { "src/parser.c" },
      requires_generate_from_grammar = true,
    },
    filetype = "felidae",
  }

  if vim.fn.exists(":TSInstall") == 2 then
    return true, "run :TSInstall felidae to build the grammar (needs tree-sitter-cli and a C compiler)"
  end
  return true, "registered felidae with nvim-treesitter"
end

return M
