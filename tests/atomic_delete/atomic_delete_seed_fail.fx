# Seeds three departments, one referenced by a Link, then deletes all of them.
# The statement must fail on the referenced row without leaving the rows
# before it deleted.
class Department
    key(id).
    def id: string.
    def kind: string.
end

class Employee
    key(id).
    def id: string.
end

def Department(id: "d1", kind: "x").
def Department(id: "d2", kind: "x").
def Department(id: "d3", kind: "x").
def Employee(id: "e1").
Link(from: Employee(id: "e1"), to: Department(id: "d3"), properties: {kind: "works_in"}).

def main() =>
    return Department.where(kind: "x").delete().
end
