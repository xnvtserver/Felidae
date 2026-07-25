import "namespaced_method_import.fx"

main() =>
    result := foo.bar(value: "imported-ok")
    return (result: result)
