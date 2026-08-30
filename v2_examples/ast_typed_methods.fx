amount := 3

inspect(value: expr) =>
    if type(value) == "func_call" then
        return "call"
    else
        return type(value)

validateDeclaration(target: stmt, body: stmts) =>
    where type(target) == "func"
    where type(body) == "stmts"
    return 1.0

@validateDeclaration()
decorated() =>
    return "decorated"

@mixfix(
    pattern: "{value: expr} inspected"
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
        func_call: functionCall,
        logical: logical,
        arithmetic: arithmetic,
        postfix_logical: postfixLogical,
        testing: testing,
        annotation: decorated()
    )
