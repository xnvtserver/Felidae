Employee(name: "Alice", role: "Engineer").
Employee(name: "Alice", role: "Manager").
Employee(name: "Bob", role: "Manager").
Employee(name: "Bob", role: "Architect").
Employee(name: "Carol", role: "Designer").
Employee(name: "Dana", role: "Engineer").

TechnicalOrManager(name: name) =>
    Employee(name: name, role: "Engineer")
    | Employee(name: name, role: "Manager").

TechnicalArchitectManager(name: name) =>
    (Employee(name: name, role: "Engineer") 
    | Employee(name: name, role: "Architect")),
    Employee(name: name, role: "Manager").

SameNamedRole(input: Employee) =>
    e := input,
    e.role == "Engineer",
    system.print(Employee(name: e.name, role: e.role)),
    return (
        name: e.name,
        role: e.role
    )
else
    e.role == "Architect",
    system.print(Employee(name: e.name, role: e.role)),
    return (
        name: e.name,
        role: e.role
    )
else
    e.role == "Manager",
    system.print(Employee(name: e.name, role: e.role)),
    return (
        name: e.name,
        role: e.role
    )
else
    return (
        name: e.name,
        role: "Unsupported"
    ).

FallbackEngineer(name: string, role: string) =>
    employee := {__type: "Employee", name: "Alice", role: "Engineer"},
    SameNamedRole(input: employee, name: name, role: role).

FallbackArchitect(name: string, role: string) =>
    employee := {__type: "Employee", name: "Bob", role: "Architect"},
    SameNamedRole(input: employee, name: name, role: role).

FallbackUnsupported(name: string, role: string) =>
    employee := {__type: "Employee", name: "Carol", role: "Designer"},
    SameNamedRole(input: employee, name: name, role: role).
