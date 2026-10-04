class Entity
    key(id).
    def id: string.
end

class Employee extends Entity
    def name: string.
end

class Entity.Note
    key(id).
    def id: string.
    def text: string.
end

def Employee(id: "e1", name: "Ada").
def Entity.Note(id: "n1", text: "related name only").

def main() =>
    return (
        entities: Entity.count(),
        notes: Entity.Note.count()
    ).
end
