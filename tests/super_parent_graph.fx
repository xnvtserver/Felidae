class Entity
    key(id).
    def id: string.
    def label: string.

    def description() =>
        this.label.
    end

    def root_id() =>
        super().id.
    end
end

class Employee extends Entity
    def department: string.

    def description() =>
        super().description() + ":" + this.department.
    end

    def parent_id() =>
        super().id.
    end
end

def main() =>
    def employee := Employee(id: "employee-1", label: "Ada", department: "Research").
    (
        description: employee.description(),
        parent_id: employee.parent_id(),
        root_id: employee.root_id()
    ).
end
