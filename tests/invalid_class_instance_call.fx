class Employee
    key(id)
    id: string

    def get_id() =>
        return this.id
    end
end

def main() =>
    return Employee.get_id()
end
