class Department
    key(id)
    id: string
end

class Employee
    key(id)
    id: string
end

Department(id: "d1")
Employee(id: "e1")
Link(from: Employee(id: "e1"), to: Department(id: "d1"))

def main() =>
    return Department.where(id: "d1").delete()
end
