amount := 3

inspect(value: expr) =>
    if type(value) == "function_call" then
        return "call"
    else
        return type(value)

validateDeclaration(target: stmt, body: stmts) =>
    where type(target) == "method_declaration"
    where type(body) == "statement_block"
    return true

@validateDeclaration()
decorated() =>
    return "decorated"

@overload(
    operator: inspectExpression,
    pattern: "{value} inspected",
    type: postfix,
    captures: {value: expr},
    result: string,
    precedence: prefix,
    associativity: none,
    cardinality: one,
    effects: pure,
    visibility: private
)
inspectExpressionKind() =>
    return type(value)

main() =>
    functionCall := inspect(value: str.concat(left: "a", right: "b"))
    logical := inspect(value: 2 < 3)
    arithmetic := inspect(value: 2 + amount)
    postfixLogical := (2 < 3) inspected
    testing := inspect(value: print("Hello"))
    return (
        function_call: functionCall,
        logical: logical,
        arithmetic: arithmetic,
        postfix_logical: postfixLogical,
        testing: testing,
        annotation: decorated()
    )
