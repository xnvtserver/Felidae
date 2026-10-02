class City
    key(country, code)
    country: string
    code: string
    name: string
end

class Employee
    key(id)
    id: string
end

City(country: "IN", code: "BLR", name: "Bengaluru")
Employee(id: "e1")
Link(
    from: Employee(id: "e1"),
    to: City(country: "IN", code: "BLR"),
    properties: {kind: "located_in"}
)

def main() =>
    rows := City().where(code: "BLR")
        .join(properties: {kind: "located_in"}, direction: backward.class),
    first := rows.get(position: 0),
    return (
        count: rows.len(),
        city: first.left.name,
        employee: first.right.id
    )
end
