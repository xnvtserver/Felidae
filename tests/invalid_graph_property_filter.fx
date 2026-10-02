class Employee
    key(id)
    id: string
end

Employee(id: "employee-1")

def main() =>
    return Employee().join(properties: "not-a-map", direction: forward.class)
end
