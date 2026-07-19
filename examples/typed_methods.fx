Employee(name: "Alice").

add(x: number, y: number) =>
    return(output: x + y).

addDecimal(x: decimal, y: decimal) =>
    return(output: x + y).

echoEmployee(input: Employee) =>
    return(name: input.name).

TypedAdd(value: value) =>
    add(x: 2, y: 3, output: value).

TypedDecimalAdd(value: value) =>
    addDecimal(x: 2.5, y: 0.5, output: value).

TypedEmployeeName(value: value) =>
    echoEmployee(input: {__type: "Employee", name: "Alice"}, name: value).
