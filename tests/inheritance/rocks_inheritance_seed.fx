class Entity
    key(id).
    def id: string.
end

class Employee extends Entity
    def name: string.
end

def Entity(id: "root").
def Employee(id: "e1", name: "Ada").

def main() =>
    Entity.count().
end
