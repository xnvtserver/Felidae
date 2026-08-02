" Vim filetype plugin for Felidae (.fx)

if exists('b:did_ftplugin')
  finish
endif
let b:did_ftplugin = 1

setlocal commentstring=#\ %s
setlocal comments=:#
setlocal iskeyword+=:
setlocal shiftwidth=4
setlocal tabstop=4
setlocal expandtab

" Neovim only: report whether felidae_debug --lsp is attached. Registration
" itself is opt-in via `require("felidae.lsp").setup()` - see README.md.
if has('nvim')
  command! -buffer FelidaeLspStatus lua require("felidae.lsp").status()
endif

command! -buffer FelidaeFormat call felidae#FormatBuffer()
command! -buffer FelidaeRun call felidae#Run()
command! -buffer FelidaeCheck call felidae#Check()
command! -buffer FelidaeVisualize call felidae#Visualize()

nnoremap <buffer> <silent> <LocalLeader>ff :FelidaeFormat<CR>
nnoremap <buffer> <silent> <LocalLeader>fr :FelidaeRun<CR>
nnoremap <buffer> <silent> <LocalLeader>fc :FelidaeCheck<CR>
nnoremap <buffer> <silent> <LocalLeader>fv :FelidaeVisualize<CR>

let b:undo_ftplugin = 'setlocal commentstring< comments< iskeyword< shiftwidth< tabstop< expandtab<'
      \ . ' | delcommand FelidaeFormat | delcommand FelidaeRun | delcommand FelidaeCheck | delcommand FelidaeVisualize'
      \ . ' | nunmap <buffer> <LocalLeader>ff | nunmap <buffer> <LocalLeader>fr'
      \ . ' | nunmap <buffer> <LocalLeader>fc | nunmap <buffer> <LocalLeader>fv'
