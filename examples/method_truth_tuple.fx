Employee(name: "Alice", role: "Engineer", manager: "Bob").
Employee(name: "Alice", role: "Manager", manager: nil).
Employee(name: "Nina", role: "Architect", manager: nil).
Employee(name: "Carol", role: "Engineer", manager: nil).

HasManager(employee: e, name: string) =>
    name == e.name,
    e.manager != nil.

TechnicalArchitectManager(name: name) =>
    (Employee(name: name, role: "Engineer")
    | Employee(name: name, role: "Architect")),
    Employee(name: name, role: "Manager").

Ancestor(x: x, y: y) =>
    Parent(parent: x, child: y).

Parent(parent: "Asha", child: "Dev").

NeverMatches() =>
    Employee(name: "Missing", role: "Ghost").

OuterFalse() =>
    NeverMatches().

PrintOnce() =>
    system.print(value: "print once").

main(arguments: system.stdin) =>
    alice := Employee(name: "Alice", role: "Engineer", manager: "Bob"),
    carol := Employee(name: "Carol", role: "Engineer", manager: nil),
    adults := lambda([alice, carol], p => p.name),
    status := system.print(value: "empty declaration loaded"),
    return (
        manager_true: HasManager(employee: alice),
        manager_false: HasManager(employee: carol),
        technical_manager: TechnicalArchitectManager(name: "Alice"),
        ancestor_true: Ancestor(x: "Asha", y: "Dev"),
        nested_false: OuterFalse(),
        print_once: PrintOnce(),
        lambda_result: adults,
        status: status,
        explicit_true: true
    ).
