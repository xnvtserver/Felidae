class Entity
    def id: string.

    def identity() =>
        this.id.
    end
end

class NamedEntity extends Entity
    def name: string.
end

def Employee extend NamedEntity(
    id: "employee-1",
    name: "Ada",
    role: "analyst"
).

def main() =>
    def employee := Employee.get(pos: 0).
    (
        id: employee.identity(),
        name: employee.name,
        role: employee.role,
        visible_as_entity: Entity.count()
    ).
end
