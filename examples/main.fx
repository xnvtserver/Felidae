import "family.fx".

Employee(name: "Alice", role: "Engineer", office: "SEA")
Employee(name: "Bob", role: "Manager", office: "SEA")
Employee(name: "Caroline", role: "Engineer", office: "LAX")
Employee(name: "David", role: "Engineer", office: "SEA")

Engineer(input: Employee) =>
    where input.role == "Engineer",
    return (
        name: input.name,
        office: input.office
    )

SameOfficeEngineer(x: Employee, y: Employee) =>
    where x.role == "Engineer",
    where y.role == "Engineer",
    where x.office == y.office,
    where x.name != y.name,
    return (
        x: x.name,
        y: y.name,
        office: x.office
    )

PrintEmployee(input: Employee) =>
    return (
        name: input.name,
        role: input.role,
        office: input.office
    )

Engineers := lambda(Employee, employee => Engineer(input: employee))

Employees := lambda(Employee, employee => PrintEmployee(input: employee))



Ancestor(x: x, y: y) =>
    Parent(parent: x, child: y)
    return

Ancestor(x: x, y: y, mid: z) =>
    Parent(parent: x, child: z),
    Ancestor(x: z, y: y)
    return

main(arguments: system.stdin) =>
    return (
        engineers: Engineers,
        employees: Employees
    )