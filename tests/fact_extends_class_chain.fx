class Entity
    id: string

    identity() =>
        return self.id
    end
end

class NamedEntity extends Entity
    name: string
end

Employee extend NamedEntity(
    id: "employee-1",
    name: "Ada",
    role: "analyst"
)

main() =>
    employee := Employee.get(pos: 0)
    return (
        id: employee.identity(),
        name: employee.name,
        role: employee.role,
        visible_as_entity: Entity.count()
    )
end
