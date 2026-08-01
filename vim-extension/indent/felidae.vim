" Vim indent file for Felidae (.fx)
"
" This wires 'indentexpr' to felidae#Indent(), a lightweight per-line
" heuristic. For structurally exact, whole-buffer reindentation (matching
" the same algorithm shared with vs-code-extension/intellij-idea-extension)
" use :FelidaeFormat instead.

if exists('b:did_indent')
  finish
endif
let b:did_indent = 1

setlocal indentexpr=felidae#Indent(v:lnum)
setlocal indentkeys=0{,0},0),0],!^F,o,O,=else
setlocal nolisp
setlocal nosmartindent
setlocal nocindent
setlocal autoindent
