# Fact is not a library or a queryable class: facts are queried through their
# own class, as in Employee.count().
class Employee
    key(id).
    id: string.
end

Employee(id: "e1").

def main() =>
    return Fact.count().
end
