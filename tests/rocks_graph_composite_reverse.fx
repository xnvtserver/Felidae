class City
    key(country, code).
    def country: string.
    def code: string.
    def name: string.
end

class Employee
    key(id).
    def id: string.
end

def City(country: "IN", code: "BLR", name: "Bengaluru").
def Employee(id: "e1").
Link(
    from: Employee(id: "e1"),
    to: City(country: "IN", code: "BLR"),
    properties: {kind: "located_in"}
).

def main() =>
    def rows := City().where(code: "BLR")
        .join(properties: {kind: "located_in"}, direction: backward.class).
    def first := rows.get(position: 0).
    return (
        count: rows.len(),
        city: first.left.name,
        employee: first.right.id
    ).
end
