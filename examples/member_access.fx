import "system"


Employee(name: "Alice", role: "Engineer", manager: "Bob", office: "SEA")
Employee(name: "Carol", role: "Engineer", manager: nil, office: "SEA")
Employee(name: "Dave", role: "Manager", manager: nil, office: "LAX")

HasManager(employee: e, name: string) =>
    name == e.name
    e.manager != nil

NoManager(employee: e, name: string) =>
    name == e.name
    e.manager == nil

EngineerInSEA(employee: e, name: string) =>
    name == e.name
    e.role == "Engineer"
    e.office == "SEA"

ManagerValue(employee: e, value: value) =>
    e.name == "Alice"
    value == e.manager

sum(x:number,y:number) =>
    return x + y

main() =>
     all_employees := lambda(Employee, e => e)
     HasManager(all_employees, "Alice")
     NoManager(all_employees, "Carol")
     EngineerInSEA(all_employees, "Alice")
     ManagerValue(all_employees, "Bob")
    system.print(value: sum(1, 2))
