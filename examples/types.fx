Person(name: "Default", age: 0).

radius_ := Radius(value: 5,unit:"cm").
Employee(name: "Default", place: Place(name: "Default",address:"ABC location"),role: Role(name: "Default")).

Adress(name: "Default", street: "Default", city: "Default", state: "Default", zip: "00000",radius:radius_).

Employee extend Person(name: "Ravi", age: 30, role: Role(name: "Engineer")).

SampleEmployee := {__type: "Employee", __parent: "Person", name: "Ravi"}.

TypeName(employee: employee, name: string) =>
    type(value: employee, name: name).

EmployeeIsPerson(employee: employee, name: string) =>
    instanceof(value: employee, type: Person),
    name == employee.name.

EmployeeIsEmployee(employee: employee, name: string) =>
    instanceof(value: employee, type: Employee),
    name == employee.name.

SampleTypeName(name: string) =>
    TypeName(employee: SampleEmployee, name: name).

SampleEmployeeIsPerson(name: string) =>
    EmployeeIsPerson(employee: SampleEmployee, name: name).

SampleEmployeeIsEmployee(name: string) =>
    EmployeeIsEmployee(employee: SampleEmployee, name: name).

main(arguments: system.stdin) =>
    status := system.print(value: "Felidae system running!"),
    type_name := SampleTypeName(name: "Employee"),
    is_person := SampleEmployeeIsPerson(name: "Ravi"),
    is_employee := SampleEmployeeIsEmployee(name: "Ravi"),
    system.print(value: type_name),
    system.print(value: is_person),
    system.print(value: is_employee),
       
    return (
        status: status
    ).