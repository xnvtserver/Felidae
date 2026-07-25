import "system"

Employee(name: "Alice", role: "Engineer", manager: "Bob", office: "SEA")

HasManager(employee: e, name: string) =>
    system.print(value: "Checking if employee has a manager...")
    system.print(value: lambda(e,e => name == e.name))
    e.manager != nil

main(arguments: system.stdin) =>
    system.print(value: "Hello, World!")
    that_employee := lambda(Employee, e => e.office == "SEA")
    return (
        has_manager: HasManager(employee: that_employee, name: "Alice")
    )
