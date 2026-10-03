# A class is queried directly.
class Employee
    key(id).
    id: string.
end

Employee(id: "e1").
Employee(id: "e2").

def main() =>
    return Employee.count().
end
