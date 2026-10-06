class Employee
    key(id).
    def id: string.

    def get_id() =>
        this.id.
    end
end

def main() =>
    Employee.get_id().
end
