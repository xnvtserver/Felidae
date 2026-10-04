class Employee
    key(id).
    def id: string.

    def get_id() =>
        return this.id.
    end
end

def main() =>
    return Employee.get_id().
end
