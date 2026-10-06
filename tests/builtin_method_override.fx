class Employee
    key(id).
    def id: string.
    @override
    def count() =>
        77.
    end
    @override
    def join() =>
        "custom-join".
    end
end

class Department
    key(id).
    def id: string.
    def count() =>
        88.
    end
end

def main() =>
    (
        overridden_count: Employee.count(),
        overridden_join: Employee.join(),
        builtin_without_override: Department.count()
    ).
end
