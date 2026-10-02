class Entity
    key(id)
    id: string
    label: string

    def description() =>
        return this.label
    end

    def root_id() =>
        return super().id
    end
end

class Employee extends Entity
    department: string

    def description() =>
        return super().description() + ":" + this.department
    end

    def parent_id() =>
        return super().id
    end
end

def main() =>
    employee := Employee(id: "employee-1", label: "Ada", department: "Research")
    return (
        description: employee.description(),
        parent_id: employee.parent_id(),
        root_id: employee.root_id()
    )
end
