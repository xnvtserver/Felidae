class Entity
    key(id).
    id: string.
end

class Employee extends Entity
    name: string.
end

Entity(id: "root").
Employee(id: "e1", name: "Ada").

def main() =>
    return Entity.count().
end
