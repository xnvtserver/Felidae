" felidae.vim --- Formatter and run/check commands for Felidae (.fx) files
"
" Felidae has no Vim-native parser, so - like this repository's VS Code and
" IntelliJ extensions, both text-scanning rather than AST/PSI based - this
" formatter works directly on buffer text.
"
" felidae#FormatLines() is a line-for-line port of
" vs-code-extension/src/formatter.ts's formatFelidaeLines (also ported to
" Java for IntelliJ and Emacs Lisp for Emacs) - keep all four in sync. See
" that file's header comment for the full design rationale: a stack of open
" "block" levels keyed by the *original* indentation width of the line that
" opened them (a clause's own `=>`, or a nested `if <cond> then`), popped
" Python-dedent-style, with `else` pairing at (not popping) the level whose
" width it exactly matches; bracket/brace/paren continuations tracked the
" same way but keyed off bracket nesting, with multiple brackets opened on
" one line counting as a single extra level.

let s:openers = '([{'
let s:closers = ')]}'

" Blanks out string-literal contents and "#" comments (preserving length) so
" bracket/keyword scanning never misreads a `(` or `=>` that only appears
" inside a string or a comment.
function! felidae#MaskLine(line) abort
  let l:len = strchars(a:line)
  let l:chars = split(a:line, '\zs')
  let l:out = repeat([' '], l:len)
  let l:i = 0
  let l:in_string = 0
  while l:i < l:len
    let l:ch = l:chars[l:i]
    if l:in_string
      if l:ch ==# '\' && l:i + 1 < l:len
        let l:out[l:i] = 'x'
        let l:out[l:i + 1] = 'x'
        let l:i += 2
        continue
      elseif l:ch ==# '"'
        let l:in_string = 0
        let l:out[l:i] = '"'
        let l:i += 1
        continue
      else
        let l:out[l:i] = 'x'
        let l:i += 1
        continue
      endif
    elseif l:ch ==# '#'
      let l:i = l:len
    elseif l:ch ==# '"'
      let l:in_string = 1
      let l:out[l:i] = '"'
      let l:i += 1
    else
      let l:out[l:i] = l:ch
      let l:i += 1
    endif
  endwhile
  return join(l:out, '')
endfunction

" Column width of a line's leading whitespace, expanding tabs to &shiftwidth
" (falls back to 4 when unset).
function! felidae#LeadingWidth(line) abort
  let l:width = 0
  let l:tabwidth = &shiftwidth ? &shiftwidth : 4
  for l:ch in split(a:line, '\zs')
    if l:ch ==# ' '
      let l:width += 1
    elseif l:ch ==# "\t"
      let l:width += l:tabwidth
    else
      break
    endif
  endfor
  return l:width
endfunction

" Non-zero if the (masked) line ends with a depth-0 `=>` or word-boundary
" `then` - meaning "this block's body continues on later lines." One with
" content after the arrow/keyword on the same line (`Foo() => return`,
" `if x then return 1`) is a complete inline block - nothing to open.
function! felidae#OpensBlock(masked) abort
  let l:chars = split(a:masked, '\zs')
  let l:len = len(l:chars)
  let l:local_depth = 0
  let l:tail_end = -1
  let l:i = 0
  while l:i < l:len
    let l:ch = l:chars[l:i]
    if stridx(s:openers, l:ch) >= 0
      let l:local_depth += 1
    elseif stridx(s:closers, l:ch) >= 0
      let l:local_depth -= 1
    elseif l:local_depth == 0 && l:ch ==# '=' && l:i + 1 < l:len && l:chars[l:i + 1] ==# '>'
      let l:tail_end = l:i + 2
    elseif l:local_depth == 0 && l:i + 3 < l:len
          \ && l:chars[l:i] ==# 't' && l:chars[l:i + 1] ==# 'h'
          \ && l:chars[l:i + 2] ==# 'e' && l:chars[l:i + 3] ==# 'n'
          \ && (l:i == 0 || l:chars[l:i - 1] =~# '\s')
          \ && (l:i + 4 >= l:len || l:chars[l:i + 4] !~# '[A-Za-z0-9_]')
      let l:tail_end = l:i + 4
    elseif l:local_depth == 0 && l:i + 4 == l:len
          \ && l:chars[l:i] ==# 't' && l:chars[l:i + 1] ==# 'h'
          \ && l:chars[l:i + 2] ==# 'e' && l:chars[l:i + 3] ==# 'n'
          \ && (l:i == 0 || l:chars[l:i - 1] =~# '\s')
      let l:tail_end = l:i + 4
    endif
    let l:i += 1
  endwhile
  if l:tail_end < 0
    return 0
  endif
  return join(l:chars[l:tail_end :], '') =~# '^\s*$'
endfunction

" Formats a list of EOL-free lines, returning a new list.
function! felidae#FormatLines(raw_lines) abort
  let l:frame_widths = []   " stack (list; end = top) of open block head-widths
  let l:bracket_levels = [] " stack of raw-bracket-depth pop thresholds
  let l:raw_depth = 0
  let l:output = []
  let l:previous_blank = 1

  for l:raw_line in a:raw_lines
    let l:trimmed = trim(l:raw_line)

    if empty(l:trimmed)
      call add(l:output, '')
      let l:previous_blank = 1
      continue
    endif

    let l:masked = felidae#MaskLine(l:raw_line)
    let l:is_comment = l:trimmed[0] ==# '#'
    let l:is_bare_else = l:trimmed ==# 'else'
    " A comment's own column is only trustworthy as a dedent signal when it
    " opens a new paragraph (preceded by a blank line, or file start) - a
    " leading doc-comment for the next top-level declaration. A comment
    " glued directly under body code with no blank line keeps whatever
    " depth is already active, like any other annotation.
    let l:comment_trusts_column = l:is_comment && l:previous_blank
    let l:previous_blank = 0

    if l:raw_depth == 0 && (!l:is_comment || l:comment_trusts_column)
      let l:width = felidae#LeadingWidth(l:raw_line)
      if l:is_bare_else
        while !empty(l:frame_widths) && l:width < l:frame_widths[-1]
          call remove(l:frame_widths, -1)
        endwhile
      else
        while !empty(l:frame_widths) && l:width <= l:frame_widths[-1]
          call remove(l:frame_widths, -1)
        endwhile
      endif
    endif

    let l:depth_units = 0
    if l:is_bare_else && !empty(l:frame_widths)
      let l:depth_units = len(l:frame_widths) - 1 + len(l:bracket_levels)
    else
      let l:mchars = split(l:masked, '\zs')
      let l:mlen = len(l:mchars)
      let l:idx = 0
      while l:idx < l:mlen && (l:mchars[l:idx] ==# ' ' || l:mchars[l:idx] ==# "\t")
        let l:idx += 1
      endwhile
      while l:idx < l:mlen && stridx(s:closers, l:mchars[l:idx]) >= 0
        let l:raw_depth -= 1
        while !empty(l:bracket_levels) && l:raw_depth <= l:bracket_levels[-1]
          call remove(l:bracket_levels, -1)
        endwhile
        let l:idx += 1
      endwhile
      let l:depth_units = len(l:frame_widths) + len(l:bracket_levels)

      let l:depth_after_leading = l:raw_depth
      while l:idx < l:mlen
        let l:ch = l:mchars[l:idx]
        if stridx(s:openers, l:ch) >= 0
          let l:raw_depth += 1
        elseif stridx(s:closers, l:ch) >= 0
          let l:raw_depth -= 1
          while !empty(l:bracket_levels) && l:raw_depth <= l:bracket_levels[-1]
            call remove(l:bracket_levels, -1)
          endwhile
        endif
        let l:idx += 1
      endwhile
      if l:raw_depth > l:depth_after_leading
        " Threshold = this line's own final depth - 1, so a single closer
        " (wherever it lands) is enough to pop this level, regardless of
        " how many raw brackets this one line net-opened.
        call add(l:bracket_levels, l:raw_depth - 1)
      endif
    endif

    if l:depth_units < 0
      let l:depth_units = 0
    endif
    let l:shiftwidth = &shiftwidth ? &shiftwidth : 4
    call add(l:output, repeat(' ', l:depth_units * l:shiftwidth) . l:trimmed)

    if l:raw_depth == 0 && !l:is_comment && felidae#OpensBlock(l:masked)
      call add(l:frame_widths, felidae#LeadingWidth(l:raw_line))
    endif
  endfor

  " Collapse 2+ blank lines to one, and drop trailing blank lines.
  let l:collapsed = []
  for l:line in l:output
    if empty(l:line) && !empty(l:collapsed) && empty(l:collapsed[-1])
      continue
    endif
    call add(l:collapsed, l:line)
  endfor
  while !empty(l:collapsed) && empty(l:collapsed[-1])
    call remove(l:collapsed, -1)
  endwhile

  return l:collapsed
endfunction

" Reformats the current buffer in place, preserving cursor line when unchanged.
function! felidae#FormatBuffer() abort
  let l:original = getline(1, '$')
  let l:formatted = felidae#FormatLines(l:original)
  if l:formatted ==# l:original
    return
  endif
  let l:save_cursor = getcurpos()
  call setline(1, l:formatted)
  if line('$') > len(l:formatted)
    execute (len(l:formatted) + 1) . ',$delete _'
  endif
  call setpos('.', l:save_cursor)
endfunction

function! s:ExecutablePath(var, default) abort
  return exists(a:var) ? eval(a:var) : a:default
endfunction

function! felidae#Run() abort
  update
  let l:exe = s:ExecutablePath('g:felidae_interpreter_path', 'felidae')
  execute '!' . shellescape(l:exe) . ' ' . shellescape(expand('%:p'))
endfunction

function! felidae#Check() abort
  update
  let l:exe = s:ExecutablePath('g:felidae_debug_interpreter_path', 'felidae_debug')
  execute '!' . shellescape(l:exe) . ' --check ' . shellescape(expand('%:p'))
endfunction

function! felidae#Visualize() abort
  update
  let l:exe = s:ExecutablePath('g:felidae_celidae_path', 'celidae')
  execute '!' . shellescape(l:exe) . ' --html ' . shellescape(expand('%:p'))
endfunction

" Lightweight per-line indent heuristic for 'indentexpr', distinct from
" felidae#FormatBuffer()'s full structural pass: indent relative to the
" nearest non-blank line above, adjusted for unclosed brackets and a
" trailing block-opening `=>`/`then` on that line, then dedented if the
" current line itself opens with a closer or is a bare `else`.
function! felidae#Indent(lnum) abort
  let l:shiftwidth = &shiftwidth ? &shiftwidth : 4
  let l:prevlnum = prevnonblank(a:lnum - 1)
  if l:prevlnum == 0
    return 0
  endif
  let l:prev = getline(l:prevlnum)
  let l:level = felidae#LeadingWidth(l:prev) / l:shiftwidth
  let l:masked_prev = felidae#MaskLine(l:prev)
  if felidae#OpensBlock(l:masked_prev)
    let l:level += 1
  endif
  for l:ch in split(l:masked_prev, '\zs')
    if stridx(s:openers, l:ch) >= 0
      let l:level += 1
    elseif stridx(s:closers, l:ch) >= 0
      let l:level = max([0, l:level - 1])
    endif
  endfor

  let l:current = trim(getline(a:lnum))
  if l:current =~# '^[)\]}]' || l:current ==# 'else'
    let l:level = max([0, l:level - 1])
  endif

  return max([0, l:level]) * l:shiftwidth
endfunction
