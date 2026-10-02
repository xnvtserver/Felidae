class Department
    key(id)
    id: string
end

class Employee
    key(id)
    id: string
end

Employee(id: "employee-1")
Link(
    from: Employee(id: "employee-1"),
    to: Department(id: "missing")
)
