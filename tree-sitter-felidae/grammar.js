/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: "felidae",

  extras: $ => [
    /\s/,
    $.comment
  ],

  word: $ => $.identifier,

  conflicts: $ => [
    [$.call, $.access_expression],
    [$.call_goal, $.expression]
  ],

  rules: {
    source_file: $ => repeat($.statement),

    comment: _ => token(seq("#", /.*/)),

    statement: $ => choice(
      $.import_statement,
      $.global_binding,
      $.clause
    ),

    import_statement: $ => seq(
      "import",
      choice(
        $.string,
        seq("(", repeat1($.string), ")")
      ),
      "."
    ),

    global_binding: $ => seq(
      field("name", $.identifier),
      ":=",
      field("value", $.expression),
      "."
    ),

    clause: $ => seq(
      field("head", $.call),
      optional(seq("extend", field("parent", $.identifier))),
      optional(seq("=>", field("body", $.goal_list), repeat($.else_branch))),
      "."
    ),

    else_branch: $ => seq("else", $.goal_list),

    goal_list: $ => seq(
      $.goal_conjunction,
      repeat(seq("|", $.goal_conjunction))
    ),

    goal_conjunction: $ => seq(
      $.goal,
      repeat(seq(",", $.goal))
    ),

    goal: $ => choice(
      $.group_goal,
      $.where_goal,
      $.return_goal,
      $.assignment_goal,
      $.comparison_goal,
      $.call_goal
    ),

    group_goal: $ => seq("(", $.goal_list, ")"),

    where_goal: $ => seq("where", $.comparison_goal),

    return_goal: $ => seq(
      "return",
      choice(
        seq("(", optional($.argument_list), ")"),
        $.expression
      )
    ),

    assignment_goal: $ => seq(
      choice(
        field("name", $.identifier),
        field("targets", $.assignment_targets)
      ),
      ":=",
      choice($.call_goal, $.expression)
    ),

    assignment_targets: $ => seq(
      $.assignment_target,
      repeat1(seq(",", $.assignment_target))
    ),

    assignment_target: $ => seq(
      field("name", $.identifier),
      optional(seq(":", field("type", $.identifier)))
    ),

    comparison_goal: $ => prec.left(1, seq(
      $.expression,
      field("operator", choice("==", "!=", "<", "<=", ">", ">=")),
      $.expression
    )),

    call_goal: $ => $.call,

    call: $ => prec(2, seq(
      field("function", $.qualified_identifier),
      "(",
      optional($.argument_list),
      ")"
    )),

    argument_list: $ => seq(
      $.argument,
      repeat(seq(",", $.argument))
    ),

    argument: $ => choice(
      seq(field("key", $.identifier), ":", field("value", $.expression)),
      $.expression
    ),

    expression: $ => choice(
      $.lambda_expression,
      $.binary_expression,
      $.access_expression,
      $.call,
      $.map,
      $.array,
      $.string,
      $.number,
      $.nil,
      $.boolean,
      $.anonymous,
      $.identifier,
      seq("(", $.expression, ")")
    ),

    lambda_expression: $ => seq(
      "lambda",
      "(",
      field("source", $.expression),
      ",",
      field("item", $.identifier),
      "=>",
      field("body", $.expression),
      optional(seq(
        field("operator", choice("==", "!=", "<", "<=", ">", ">=")),
        field("right", $.expression)
      )),
      ")"
    ),

    binary_expression: $ => choice(
      prec.left(4, seq($.expression, choice("*", "/"), $.expression)),
      prec.left(3, seq($.expression, choice("+", "-"), $.expression))
    ),

    access_expression: $ => prec.left(5, seq(
      $.expression,
      choice(".", ":"),
      field("field", $.identifier)
    )),

    map: $ => seq(
      "{",
      optional(seq($.map_entry, repeat(seq(",", $.map_entry)))),
      "}"
    ),

    map_entry: $ => seq(
      field("key", $.identifier),
      ":",
      field("value", $.expression)
    ),

    array: $ => seq(
      "[",
      optional(seq($.expression, repeat(seq(",", $.expression)))),
      "]"
    ),

    qualified_identifier: $ => seq(
      $.identifier,
      repeat(seq(choice(".", ":"), $.identifier))
    ),

    identifier: _ => /[A-Za-z_][A-Za-z0-9_]*/,

    anonymous: _ => "_",
    nil: _ => "nil",
    boolean: _ => choice("true", "false"),

    string: _ => token(seq(
      '"',
      repeat(choice(/[^"\\]/, /\\./)),
      '"'
    )),

    number: _ => token(seq(
      optional("-"),
      /\d+/,
      optional(seq(".", /\d+/))
    ))
  }
});
