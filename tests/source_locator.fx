class Employee
    key(id).
    def id: string.
    def get_data() =>
        this.id.
    end
end

def main() =>
    fx.interpret(
        class: Employee.class,
        function: get_data.function,
        object: new Employee(id: "employee-1"),
        file: "source_locator.fx",
        line: 4
    ).
end
