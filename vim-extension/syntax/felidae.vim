" Vim syntax file for Felidae (.fx)

if exists('b:current_syntax')
  finish
endif

syn case match

syn match felidaeComment "#.*$" contains=@Spell
syn region felidaeString start=/"/ skip=/\\./ end=/"/ contains=@Spell
syn match felidaeNumber "\v<-?\d+(\.\d+)?>"

syn keyword felidaeKeyword extend where if else return lambda then import
syn keyword felidaeConstant nil true false

" name(...) or name(...) => - declaration head.
syn match felidaeDeclaration "^\s*\zs[A-Za-z_][A-Za-z0-9_:.]*\ze\s*("

" name := ... binding.
syn match felidaeBinding "\v<[A-Za-z_][A-Za-z0-9_]*>\ze\s*\:\="

" Standard-library module names before `.` or `:`.
syn keyword felidaeLibrary array comparison console csv db exception fact
      \ fact_analysis file flibrary fn group gtk http json list logic math
      \ ml package pair plot prelude probability process qt set smoke str
      \ system thread wordnet nextgroup=felidaeLibraryAccessor

syn match felidaeLibraryAccessor "[.:]" contained

syn keyword felidaeType any array bool boolean decimal double float int number string

" Capitalized identifiers read as fact/type names.
syn match felidaeTypeName "\v<[A-Z][A-Za-z0-9_]*>"

syn match felidaeOperator "=>\|:=\|==\|!=\|<=\|>="

hi def link felidaeComment      Comment
hi def link felidaeString       String
hi def link felidaeNumber       Number
hi def link felidaeKeyword      Keyword
hi def link felidaeConstant     Constant
hi def link felidaeDeclaration  Function
hi def link felidaeBinding      Identifier
hi def link felidaeLibrary      Special
hi def link felidaeLibraryAccessor Delimiter
hi def link felidaeType         Type
hi def link felidaeTypeName     Type
hi def link felidaeOperator     Operator

let b:current_syntax = 'felidae'
