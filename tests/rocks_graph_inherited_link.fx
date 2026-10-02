class Department
    key(id)
    id: string
    name: string
end

class Person
    key(id)
    id: string
end

class Engineer extends Person
    specialty: string
end

Department(id: "d1", name: "Research")
Engineer(id: "e1", specialty: "storage")
Link(from: Engineer(id: "e1"), to: Department(id: "d1"), properties: {kind: "works_in"})

def main() =>
    rows := Engineer().join(properties: {kind: "works_in"}, direction: forward.class),
    return (
        count: rows.len(),
        department: rows.get(position: 0).right.name
    )
end
