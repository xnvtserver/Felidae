 import "system".

Employees := [
    Employee(name: "Alice", role: "Engineer", manager: "Bob", office: "SEA"),
    Employee(name: "Carol", role: "Engineer", manager: nil, office: "SEA"),
    Employee(name: "Dave", role: "Manager", manager: nil, office: "LAX")
].

EmployeeAt(index: number, employee: employee) =>
    array:get(data: Employees, position: index, access: employee).

HasManager(employee: e, name: string) =>
    name == e.name,
    e.manager != nil.

NoManager(employee: e, name: string) =>
    name == e.name,
    e.manager == nil.

EngineerInSEA(employee: e, name: string) =>
    name == e.name,
    e.role == "Engineer",
    e.office == "SEA".

ManagerValue(employee: e, value: value) =>
    e.name == "Alice",
    value == e.manager.

sum(x: number, y: number) =>
    return x + y.

main() =>
    e1 := array:get(data: Employees, position: 0), HasManager(e1, "Alice"),
    e2 := array:get(data: Employees, position: 1), NoManager(e2, "Carol"),
    e3 := array:get(data: Employees, position: 0), EngineerInSEA(e3, "Alice"),
    e4 := array:get(data: Employees, position: 0), ManagerValue(e4, "Bob"),
    system.print(value: e1),
    system.print(value:" ... \n"),
    system.print(value: e2),
    system.print(value:" ... \n"),
    system.print(value: e3),
    system.print(value:" ... \n"),
    system.print(value: e4),
    system.print(value: sum(1, 2)).
