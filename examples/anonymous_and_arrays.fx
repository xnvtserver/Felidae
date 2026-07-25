Employee(name: "Alice", role: "Engineer", office: "SEA")
Employee(name: "Bob", role: "Manager", office: "LAX")

EmployeeSea(name: string) =>
    Employee(name: name, role: _, office: "SEA")
    return

NamedEmployee(name: "Alice", role: "Engineer", office: "SEA")
NamedEmployee(name: "Bob", role: "Manager", office: "LAX")

NamedEmployeeSea(name: string) =>
    NamedEmployee(name: name, role: _, office: "SEA")
    return

ArrayAssigned(value: value) =>
    array1 := fn:array(data: [1, 2, 3, 4])
    array:get(data: array1, position: 2, access: value)
    return

ArrayAssignedRaw(array: array1) =>
    array1 := fn:array(data: [1, 2, 3, 4])
    return

main() =>
    ArrayAssigned(value: 3)
# ArrayAssignedRaw(array: [1, 2, 3, 4])
    EmployeeSea(name: "Alice")
    NamedEmployeeSea(name: "Alice")
    return
