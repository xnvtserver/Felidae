class Department
    key(id).
    def id: string.
end

class Employee
    key(id).
    def id: string.
end

def Department(id: "d1").
def Employee(id: "e1").
Link(from: Employee(id: "e1"), to: Department(id: "d1")).

def main() =>
    return Department.where(id: "d1").delete().
end
