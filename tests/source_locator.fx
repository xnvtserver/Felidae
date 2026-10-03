class Employee
    key(id).
    id: string.
    def get_data() =>
        return this.id.
    end
end

def main() =>
    return fx.interpret(
        class: Employee.class,
        function: get_data.function,
        object: new Employee(id: "employee-1"),
        file: "source_locator.fx",
        line: 4
    ).
end
