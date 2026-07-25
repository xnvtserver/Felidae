Employee(name: "Alice", role: "Engineer", manager: "Bob")
Employee(name: "Carol", role: "Engineer", manager: nil)

HasManager(employee: e, name: string) =>
    name == e.name
    e.manager != nil
    return

HeadManagerName(name: string) =>
    employee := {__type: "Employee", name: "Alice", role: "Engineer", manager: "Bob"}
    HasManager(employee: employee, name: name)
    return

main(arguments: system.stdin) =>
     system.print(value: "Hello, World!")
     employee := Employee(name: "Alice", role: "Engineer", manager: "Bob")
    return (
        has_manager: HasManager(employee: employee)
    )
