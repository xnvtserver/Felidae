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
