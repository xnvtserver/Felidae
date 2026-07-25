Employee(name: "Carol", role: "Engineer", manager: nil)

HasManager(employee: e) =>
    e.manager != nil

main() =>
    employee := Employee(name: "Carol", role: "Engineer", manager: nil)
    result := HasManager(employee: employee)
    return (
        result: result
    )
