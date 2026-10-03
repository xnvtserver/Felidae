class Entity
    key(id).
    id: string.
end

class Employee extends Entity
    name: string.
end

class Entity.Note
    key(id).
    id: string.
    text: string.
end

Employee(id: "e1", name: "Ada").
Entity.Note(id: "n1", text: "related name only").

def main() =>
    return (
        entities: Entity.count(),
        notes: Entity.Note.count()
    ).
end
