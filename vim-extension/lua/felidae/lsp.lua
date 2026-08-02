-- Neovim client for `felidae_debug --lsp`.
--
-- The server provides diagnostics, document symbols and go-to-definition from
-- the real parse, so attaching it gives `gd`, `:lua vim.lsp.buf.document_symbol()`,
-- and inline diagnostics without this plugin reimplementing any of it. The
-- Vimscript syntax/indent/format files keep working with or without it, so a
-- setup with no built felidae_debug loses nothing it had before.
--
-- Usage:
--   require("felidae.lsp").setup()                       -- resolve on $PATH
--   require("felidae.lsp").setup({ cmd = "/path/to/felidae_debug" })

local M = {}

local function executable(cmd)
  return vim.fn.executable(cmd) == 1
end

--- Resolves the server binary: explicit option, then g:felidae_debug_interpreter_path,
--- then a build/ directory next to the current working directory, then $PATH.
local function resolve_command(opts)
  if opts.cmd and executable(opts.cmd) then return opts.cmd end

  local configured = vim.g.felidae_debug_interpreter_path
  if configured and executable(configured) then return configured end

  local suffix = vim.fn.has("win32") == 1 and ".exe" or ""
  local local_build = vim.fn.getcwd() .. "/build/felidae_debug" .. suffix
  if executable(local_build) then return local_build end

  local bare = "felidae_debug" .. suffix
  if executable(bare) then return bare end
  return nil
end

--- Attaches the language server to Felidae buffers.
--- @param opts table|nil { cmd = string, root_dir = string, on_attach = function }
--- @return boolean started, string message
function M.setup(opts)
  opts = opts or {}

  if vim.fn.has("nvim-0.8") ~= 1 then
    return false, "felidae: Neovim 0.8+ is required for the language server"
  end

  local cmd = resolve_command(opts)
  if not cmd then
    return false,
      "felidae: felidae_debug not found; set g:felidae_debug_interpreter_path "
        .. "or pass { cmd = ... }. Syntax, indent and :FelidaeFormat still work."
  end

  local group = vim.api.nvim_create_augroup("FelidaeLsp", { clear = true })
  vim.api.nvim_create_autocmd("FileType", {
    group = group,
    pattern = "felidae",
    callback = function(args)
      local root = opts.root_dir
        or vim.fs.dirname(
          vim.fs.find({ ".git", "build" }, {
            upward = true,
            path = vim.api.nvim_buf_get_name(args.buf),
          })[1] or vim.api.nvim_buf_get_name(args.buf)
        )

      vim.lsp.start({
        name = "felidae",
        cmd = { cmd, "--lsp" },
        root_dir = root,
        on_attach = opts.on_attach,
      }, { bufnr = args.buf })
    end,
  })

  return true, "felidae: language server registered (" .. cmd .. " --lsp)"
end

--- :FelidaeLspStatus - report whether the server is attached to this buffer.
function M.status()
  local clients = vim.lsp.get_clients
      and vim.lsp.get_clients({ bufnr = 0, name = "felidae" })
    or vim.lsp.get_active_clients({ bufnr = 0, name = "felidae" })
  if #clients == 0 then
    vim.notify("felidae: no language server attached to this buffer", vim.log.levels.WARN)
  else
    vim.notify("felidae: language server attached (id " .. clients[1].id .. ")", vim.log.levels.INFO)
  end
end

return M
