import "smoke".

GoalInput := "goal ok".

NativeEchoExpression(value: string) =>
    echoed := smoke.echo(value: value),
    return echoed.

NativeEchoGoal(value: string, access: string) =>
    smoke.echo(value: value, access: access),
    return access.

main() =>
    expression := NativeEchoExpression(value: "expression ok"),
    return (
        expression: expression
    ).
