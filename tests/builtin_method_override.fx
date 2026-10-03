class Employee
    key(id).
    id: string.
    @override
    def count() =>
        return 77.
    end
    @override
    def join() =>
        return "custom-join".
    end
end

class Department
    key(id).
    id: string.
    def count() =>
        return 88.
    end
end

def main() =>
    return (
        overridden_count: Employee.count(),
        overridden_join: Employee.join(),
        builtin_without_override: Department.count()
    ).
end
