; Reused verbatim from tree-sitter-felidae/queries/highlights.scm (this
; plugin's Lua module points nvim-treesitter at that grammar directly -
; see lua/felidae/init.lua - so the query capture names must match).

[
  "import"
  "extend"
  "where"
  "return"
  "else"
  "lambda"
  "then"
] @keyword

[
  "=>"
  ":="
  "=="
  "!="
  "<"
  "<="
  ">"
  ">="
  "+"
  "-"
  "*"
  "/"
  "|"
] @operator

(comment) @comment
(string) @string
(number) @number
(nil) @constant.builtin
(boolean) @constant.builtin
(anonymous) @variable.builtin

(call function: (qualified_identifier) @function)
(clause head: (call function: (qualified_identifier) @function.method))
(map_entry key: (identifier) @property)
(argument key: (identifier) @property)
(access_expression field: (identifier) @property)

((identifier) @type
 (#match? @type "^[A-Z]"))
