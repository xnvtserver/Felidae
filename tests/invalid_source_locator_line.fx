class Employee
    key(id).
    def id: string.
    def get_data() =>
        return this.id.
    end
end

def main() =>
    return fx.interpret(
        class: Employee.class,
        function: get_data.function,
        object: new Employee(id: "employee-1"),
        file: "invalid_source_locator_line.fx",
        line: 5
    ).
end
