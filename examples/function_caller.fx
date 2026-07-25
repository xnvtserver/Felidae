import "debug_modules/function_definition.fx"

CallerResult(name: name, role: role) =>
    RemoteRole(name: name, role: role)
    return

main() =>
    result := CallerResult(name: "Anu", role: "Student")
    system.print(value: result)
    return (
        result: result
    )
