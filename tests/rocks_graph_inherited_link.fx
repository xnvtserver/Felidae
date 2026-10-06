class Department
    key(id).
    def id: string.
    def name: string.
end

class Person
    key(id).
    def id: string.
end

class Engineer extends Person
    def specialty: string.
end

def Department(id: "d1", name: "Research").
def Engineer(id: "e1", specialty: "storage").
Link(from: Engineer(id: "e1"), to: Department(id: "d1"), properties: {kind: "works_in"}).

def main() =>
    def rows := Engineer().join(properties: {kind: "works_in"}, direction: forward.class).
    (
        count: rows.len(),
        department: rows.get(position: 0).right.name
    ).
end
