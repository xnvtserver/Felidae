# Seeds three departments, one referenced by a Link, then deletes all of them.
# The statement must fail on the referenced row without leaving the rows
# before it deleted.
class Department
    key(id).
    id: string.
    kind: string.
end

class Employee
    key(id).
    id: string.
end

Department(id: "d1", kind: "x").
Department(id: "d2", kind: "x").
Department(id: "d3", kind: "x").
Employee(id: "e1").
Link(from: Employee(id: "e1"), to: Department(id: "d3"), properties: {kind: "works_in"}).

def main() =>
    return Department.where(kind: "x").delete().
end
