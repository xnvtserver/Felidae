;;; felidae-mode.el --- Major mode for the Felidae language (.fx files) -*- lexical-binding: t; -*-

;; Copyright (C) 2026 Felidae Project
;; URL: https://github.com/xnvtserver/Felidae/tree/main/emacs-extension
;; Version: 0.1.0
;; Package-Requires: ((emacs "26.1"))
;; Keywords: languages

;;; Commentary:

;; Major mode for editing Felidae (.fx) source files: syntax highlighting,
;; `#'-comment support, a structural buffer formatter/beautifier, a simple
;; interactive indent-line function, and run/check commands that shell out
;; to the `felidae' and `felidae_debug' executables (mirroring the run/check
;; commands in vs-code-extension and intellij-idea-extension).
;;
;; Felidae has no Emacs-native parser available, so - exactly like the
;; VS Code and IntelliJ extensions in this repository, both of which are
;; text-scanning rather than AST/PSI based - this mode's fontification and
;; formatting work directly on buffer text.

;;; Code:

(require 'cl-lib)
(require 'compile)
(require 'subr-x)
(require 'thingatpt)

(defgroup felidae nil
  "Support for the Felidae language."
  :group 'languages
  :prefix "felidae-")

(defcustom felidae-interpreter-path "felidae"
  "Path to the Felidae interpreter executable.
May be a bare command name resolved via `exec-path', or an absolute path."
  :type 'string
  :group 'felidae)

(defcustom felidae-debug-interpreter-path "felidae_debug"
  "Path to the Felidae AST debugger/checker executable."
  :type 'string
  :group 'felidae)

(defcustom felidae-celidae-path "celidae"
  "Path to the Celidae fact-visualization executable."
  :type 'string
  :group 'felidae)

(defcustom felidae-indent-offset 4
  "Number of columns per indentation level in Felidae source."
  :type 'integer
  :group 'felidae)

;;; Syntax table

(defvar felidae-mode-syntax-table
  (let ((table (make-syntax-table)))
    (modify-syntax-entry ?_ "_" table)
    (modify-syntax-entry ?\" "\"" table)
    (modify-syntax-entry ?\\ "\\" table)
    (modify-syntax-entry ?# "<" table)
    (modify-syntax-entry ?\n ">" table)
    (modify-syntax-entry ?: "." table)
    (modify-syntax-entry ?. "." table)
    (modify-syntax-entry ?, "." table)
    (modify-syntax-entry ?| "." table)
    (modify-syntax-entry ?= "." table)
    (modify-syntax-entry ?< "." table)
    (modify-syntax-entry ?> "." table)
    (modify-syntax-entry ?+ "." table)
    (modify-syntax-entry ?- "." table)
    (modify-syntax-entry ?* "." table)
    (modify-syntax-entry ?/ "." table)
    table)
  "Syntax table for `felidae-mode'.")

;;; Font-lock

(defconst felidae-keywords
  '("extend" "where" "if" "else" "return" "lambda" "then" "import")
  "Felidae control-flow and declaration keywords.")

(defconst felidae-constants
  '("nil" "true" "false")
  "Felidae literal constants.")

(defconst felidae-type-names
  '("any" "array" "bool" "boolean" "decimal" "double" "float" "int" "number" "string")
  "Felidae primitive type annotations.")

(defconst felidae-library-names
  '("array" "comparison" "console" "csv" "db" "exception" "fact" "fact_analysis"
    "file" "flibrary" "fn" "group" "gtk" "http" "json" "list" "logic" "math"
    "ml" "package" "pair" "plot" "prelude" "probability" "process" "qt" "set"
    "smoke" "str" "system" "thread" "wordnet")
  "Felidae standard-library module names (see docs/builtin-docs.json).")

(defvar felidae-font-lock-keywords
  (list
   ;; Declaration head: `Name(...)' or `Name(...) =>' - method/fact name.
   '("^[ \t]*\\([A-Za-z_][A-Za-z0-9_:.]*\\)[ \t]*(" 1 font-lock-function-name-face)
   ;; `Name extend Parent'
   '("\\_<extend\\_>[ \t]+\\([A-Za-z_][A-Za-z0-9_]*\\)" 1 font-lock-type-face)
   ;; Global/local bindings: `name := ...'
   '("\\_<\\([A-Za-z_][A-Za-z0-9_]*\\)\\_>[ \t]*:=" 1 font-lock-variable-name-face)
   ;; Library module names before `.' or `:'
   (cons (concat "\\_<" (regexp-opt felidae-library-names) "\\_>[ \t]*[.:]")
         font-lock-builtin-face)
   (cons (regexp-opt felidae-keywords 'symbols) font-lock-keyword-face)
   (cons (regexp-opt felidae-constants 'symbols) font-lock-constant-face)
   ;; Type annotations always follow `:' in this grammar (`name: string');
   ;; Emacs regexps have no lookbehind, so just match the bareword itself.
   (cons (regexp-opt felidae-type-names 'symbols) font-lock-type-face)
   '("\\_<\\([A-Z][A-Za-z0-9_]*\\)\\_>" 1 font-lock-type-face)
   '("\\(=>\\|:=\\|==\\|!=\\|<=\\|>=\\)" 1 font-lock-keyword-face)
   ;; Named-argument keys: `key: value'
   '("\\_<\\([A-Za-z_][A-Za-z0-9_]*\\)\\_>[ \t]*:[^=]" 1 font-lock-constant-face))
  "Font-lock keyword table for `felidae-mode'.")

;;; Formatter - a structural, line-based beautifier.
;;
;; This is a line-for-line port of vs-code-extension/src/formatter.ts's
;; `formatFelidaeLines' (also ported to Java for the IntelliJ plugin as
;; FelidaeFormatter.formatLines) - keep the three in sync. See that file's
;; header comment for the full design rationale: a stack of open "block"
;; levels keyed by the *original* indentation width of the line that opened
;; them (a clause's own `=>', or a nested `if <cond> then'), popped
;; Python-dedent-style, with `else' pairing at (not popping) the level whose
;; width it exactly matches; bracket/brace/paren continuations tracked the
;; same way but keyed off bracket nesting, with multiple brackets opened on
;; one line counting as a single extra level.

(defconst felidae--openers "([{")
(defconst felidae--closers ")]}")

(defun felidae--mask-line (line)
  "Blank out string-literal contents and `#' comments in LINE.
Preserves LINE's length so bracket/keyword scanning by column offset
stays valid, while never misreading a bracket or `=>' that only
appears inside a string or a comment."
  (let ((len (length line))
        (out (make-string (length line) ?\s))
        (i 0)
        (in-string nil))
    (while (< i len)
      (let ((ch (aref line i)))
        (cond
         (in-string
          (cond
           ((and (eq ch ?\\) (< (1+ i) len))
            (aset out i ?x)
            (aset out (1+ i) ?x)
            (setq i (+ i 2)))
           ((eq ch ?\")
            (setq in-string nil)
            (aset out i ?\")
            (setq i (1+ i)))
           (t
            (aset out i ?x)
            (setq i (1+ i)))))
         ((eq ch ?#)
          (setq i len)) ; rest of line stays blank (spaces already filled)
         ((eq ch ?\")
          (setq in-string t)
          (aset out i ?\")
          (setq i (1+ i)))
         (t
          (aset out i ch)
          (setq i (1+ i))))))
    out))

(defun felidae--leading-width (line)
  "Column width of LINE's leading whitespace, expanding tabs to `felidae-indent-offset'."
  (let ((width 0) (i 0) (len (length line)))
    (catch 'done
      (while (< i len)
        (let ((ch (aref line i)))
          (cond
           ((eq ch ?\s) (setq width (1+ width)))
           ((eq ch ?\t) (setq width (+ width felidae-indent-offset)))
           (t (throw 'done width))))
        (setq i (1+ i))))
    width))

(defun felidae--opens-block-p (masked)
  "Non-nil if MASKED (a masked line) ends with a depth-0 `=>' or `then'.
Such a line means \"this block's body continues on later lines\"; one
with content after the arrow/keyword on the same line
\(`Foo() => return', `if x then return 1') is a complete inline block."
  (let ((local-depth 0) (tail-end -1) (len (length masked)) (i 0))
    (while (< i len)
      (let ((ch (aref masked i)))
        (cond
         ((cl-find ch felidae--openers) (setq local-depth (1+ local-depth)))
         ((cl-find ch felidae--closers) (setq local-depth (1- local-depth)))
         ((and (= local-depth 0) (eq ch ?=) (< (1+ i) len) (eq (aref masked (1+ i)) ?>))
          (setq tail-end (+ i 2)))
         ((and (= local-depth 0)
               (>= (- len i) 4)
               (string= (substring masked i (+ i 4)) "then")
               (or (= i 0) (memq (aref masked (1- i)) '(?\s ?\t)))
               (or (>= (+ i 4) len) (not (string-match-p "[A-Za-z0-9_]" (substring masked (+ i 4) (+ i 5))))))
          (setq tail-end (+ i 4)))))
      (setq i (1+ i)))
    (and (>= tail-end 0) (string-blank-p (substring masked tail-end)))))

(defun felidae-format-lines (raw-lines)
  "Return RAW-LINES (a list of EOL-free strings) reformatted."
  (let ((frame-widths nil)   ; stack (list, head = top) of open block head-widths
        (bracket-levels nil) ; stack of raw-bracket-depth pop thresholds
        (raw-depth 0)
        (output nil)
        (previous-blank t))
    (dolist (raw-line raw-lines)
      (let ((trimmed (string-trim raw-line)))
        (if (string-empty-p trimmed)
            (progn
              (push "" output)
              (setq previous-blank t))
          (let* ((masked (felidae--mask-line raw-line))
                 (is-comment (string-prefix-p "#" trimmed))
                 (is-bare-else (string= trimmed "else"))
                 (comment-trusts-column (and is-comment previous-blank))
                 (depth-units 0))
            (setq previous-blank nil)
            (when (and (= raw-depth 0) (or (not is-comment) comment-trusts-column))
              (let ((width (felidae--leading-width raw-line)))
                (if is-bare-else
                    (while (and frame-widths (< width (car frame-widths)))
                      (pop frame-widths))
                  (while (and frame-widths (<= width (car frame-widths)))
                    (pop frame-widths)))))
            (if (and is-bare-else frame-widths)
                (setq depth-units (+ (1- (length frame-widths)) (length bracket-levels)))
              (let ((idx 0) (len (length masked)))
                (while (and (< idx len) (memq (aref masked idx) '(?\s ?\t)))
                  (setq idx (1+ idx)))
                (while (and (< idx len) (cl-find (aref masked idx) felidae--closers))
                  (setq raw-depth (1- raw-depth))
                  (while (and bracket-levels (<= raw-depth (car bracket-levels)))
                    (pop bracket-levels))
                  (setq idx (1+ idx)))
                (setq depth-units (+ (length frame-widths) (length bracket-levels)))
                (let ((depth-after-leading raw-depth))
                  (while (< idx len)
                    (let ((ch (aref masked idx)))
                      (cond
                       ((cl-find ch felidae--openers) (setq raw-depth (1+ raw-depth)))
                       ((cl-find ch felidae--closers)
                        (setq raw-depth (1- raw-depth))
                        (while (and bracket-levels (<= raw-depth (car bracket-levels)))
                          (pop bracket-levels)))))
                    (setq idx (1+ idx)))
                  (when (> raw-depth depth-after-leading)
                    (push (1- raw-depth) bracket-levels)))))
            (when (< depth-units 0) (setq depth-units 0))
            (push (concat (make-string (* depth-units felidae-indent-offset) ?\s) trimmed) output)
            (when (and (= raw-depth 0) (not is-comment) (felidae--opens-block-p masked))
              (push (felidae--leading-width raw-line) frame-widths))))))
    (setq output (nreverse output))
    ;; Collapse 2+ blank lines to one, and drop trailing blank lines.
    (let (collapsed)
      (dolist (line output)
        (unless (and (string-empty-p line) collapsed (string-empty-p (car collapsed)))
          (push line collapsed)))
      (while (and collapsed (string-empty-p (car collapsed)))
        (pop collapsed))
      (nreverse collapsed))))

(defun felidae-format-buffer ()
  "Reformat the current buffer's indentation, blank lines, and trailing whitespace."
  (interactive)
  (let* ((original (buffer-string))
         (lines (split-string original "\n"))
         (formatted (concat (string-join (felidae-format-lines lines) "\n") "\n")))
    (unless (string= formatted original)
      (let ((point-line (line-number-at-pos)))
        (erase-buffer)
        (insert formatted)
        (goto-char (point-min))
        (forward-line (1- point-line))))))

;;; Interactive single-line indentation (a lighter-weight heuristic for
;;; typing, distinct from `felidae-format-buffer''s full structural pass).

(defun felidae--previous-code-line ()
  "Return the trimmed text of the nearest non-blank line above point, or nil."
  (save-excursion
    (forward-line -1)
    (while (and (not (bobp)) (string-blank-p (thing-at-point 'line t)))
      (forward-line -1))
    (let ((text (string-trim (or (thing-at-point 'line t) ""))))
      (unless (string-empty-p text) text))))

(defun felidae-indent-line ()
  "Indent the current line using a simple heuristic.
For whole-buffer, structurally exact reindentation use
`felidae-format-buffer' instead."
  (interactive)
  (let* ((current (string-trim (thing-at-point 'line t)))
         (previous (felidae--previous-code-line))
         (level 0))
    (when previous
      (setq level (/ (felidae--leading-width previous) felidae-indent-offset))
      (let ((masked-prev (felidae--mask-line previous)))
        (when (felidae--opens-block-p masked-prev) (setq level (1+ level)))
        (dolist (ch (append masked-prev nil))
          (cond ((cl-find ch felidae--openers) (setq level (1+ level)))
                ((cl-find ch felidae--closers) (setq level (max 0 (1- level))))))))
    (when (or (string-prefix-p ")" current) (string-prefix-p "]" current) (string-prefix-p "}" current))
      (setq level (max 0 (1- level))))
    (when (string= current "else") (setq level (max 0 (1- level))))
    (indent-line-to (* (max 0 level) felidae-indent-offset))))

;;; Run / check commands

(defun felidae-run-file ()
  "Run the current Felidae file with `felidae-interpreter-path'."
  (interactive)
  (unless buffer-file-name (user-error "Buffer is not visiting a file"))
  (save-buffer)
  (compile (mapconcat #'shell-quote-argument
                       (list felidae-interpreter-path buffer-file-name)
                       " ")))

(defun felidae-check-file ()
  "Check the current Felidae file with `felidae-debug-interpreter-path'."
  (interactive)
  (unless buffer-file-name (user-error "Buffer is not visiting a file"))
  (save-buffer)
  (compile (mapconcat #'shell-quote-argument
                       (list felidae-debug-interpreter-path "--check" buffer-file-name)
                       " ")))

(defun felidae-visualize-file ()
  "Visualize the current Felidae file's fact graph with `felidae-celidae-path'."
  (interactive)
  (unless buffer-file-name (user-error "Buffer is not visiting a file"))
  (save-buffer)
  (compile (mapconcat #'shell-quote-argument
                       (list felidae-celidae-path "--html" buffer-file-name)
                       " ")))

;;; Comments / misc

(defun felidae--setup-comments ()
  (setq-local comment-start "# ")
  (setq-local comment-end "")
  (setq-local comment-start-skip "#+[ \t]*"))

;;; Keymap

(defvar felidae-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "C-c C-f") #'felidae-format-buffer)
    (define-key map (kbd "C-c C-r") #'felidae-run-file)
    (define-key map (kbd "C-c C-c") #'felidae-check-file)
    (define-key map (kbd "C-c C-v") #'felidae-visualize-file)
    map)
  "Keymap for `felidae-mode'.")

;;;###autoload
(define-derived-mode felidae-mode prog-mode "Felidae"
  "Major mode for editing Felidae (.fx) source files.

\\{felidae-mode-map}"
  :syntax-table felidae-mode-syntax-table
  (setq-local font-lock-defaults '(felidae-font-lock-keywords))
  (setq-local indent-line-function #'felidae-indent-line)
  (setq-local indent-tabs-mode nil)
  (setq-local tab-width felidae-indent-offset)
  (felidae--setup-comments))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.fx\\'" . felidae-mode))

(provide 'felidae-mode)

;;; felidae-mode.el ends here
