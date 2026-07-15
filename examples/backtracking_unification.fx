Color(value: "red").
Color(value: "blue").

Shape(value: "circle").
Shape(value: "square").

Pair(left: "red", right: "red").
Pair(left: "red", right: "blue").
Pair(left: "green", right: "green").

Employee(name: "Alice", role: "Engineer").
Employee(name: "Bob", role: "Manager").

Wrapped(item: {name: "Alice", meta: {role: "Engineer", office: "SEA"}}).
Wrapped(item: {name: "Bob", meta: {role: "Manager", office: "LAX"}}).

Choice(color: color, shape: shape) =>
    Color(value: color),
    Shape(value: shape).

SamePair(value: value) =>
    Pair(left: value, right: value).

AnyEmployee(name: name) =>
    Employee(name: name, role: _).

NestedEmployee(name: name, role: role, office: office) =>
    Wrapped(item: {name: name, meta: {role: role, office: office}}).

ExpectedChoices := [
    {color: "red", shape: "circle"},
    {color: "red", shape: "square"},
    {color: "blue", shape: "circle"},
    {color: "blue", shape: "square"}
].

ExpectedSamePairs := ["red", "green"].

main() =>
    choiceChecks := lambda(ExpectedChoices, item => Choice(color: item.color, shape: item.shape)),
    samePairChecks := lambda(ExpectedSamePairs, item => SamePair(value: item)),
    employeeNames := lambda(Employee, employee => AnyEmployee(name: employee.name)),
    nestedEmployees := lambda(Wrapped, wrapped => NestedEmployee(
        name: wrapped.item.name,
        role: wrapped.item.meta.role,
        office: wrapped.item.meta.office
    )),
    return (
        choice_count: count(choiceChecks),
        same_pair_count: count(samePairChecks),
        employee_count: count(employeeNames),
        nested_count: count(nestedEmployees),
        choices: choiceChecks,
        same_pairs: samePairChecks,
        employees: employeeNames,
        nested: nestedEmployees
    ).
