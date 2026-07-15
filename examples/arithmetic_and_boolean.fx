import "system".

Employees := [
    Employee(name: "Alice", role: "Engineer", manager: "Bob", office: "SEA"),
    Employee(name: "Carol", role: "Engineer", manager: nil, office: "SEA")
].

AliceName := "Alice".
CarolName := "Carol".

EmployeeAt(index: number, employee: employee) =>
    array:get(data: Employees, position: index, access: employee).

HasManager(employee: e, name: string) =>
    name == e.name,
    e.manager != nil.

NoManager(employee: e, name: string) =>
    name == e.name,
    e.manager == nil.

HasManagerValue(employee: e) =>
    return HasManager(employee: e, name: "Alice").

BooleanReturnOnly() =>
    return HasManager(
        employee: Employee(name: "Alice", role: "Engineer", manager: "Bob", office: "SEA"),
        name: AliceName
    ).

ArrayAssignedFirst() =>
    first := array:get(data: Employees, position: 0),
    return first.

ArithmeticResult() =>
    return (
        add: 2 + 3,
        sub: 10 - 4,
        mul: 6 * 7,
        div: 20 / 5,
        precedence: 2 + 3 * 4,
        grouped: (2 + 3) * 4
    ).

ArithmeticReturnOnly() =>
    return ArithmeticResult().

main() =>
    return (
        first_has_manager: BooleanReturnOnly(),
        arithmetic: ArithmeticResult()
    ).
