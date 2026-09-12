class Person
    name: string

    def label() =>
        return self.name
    end
end

class Student extends Person
    grade: number

    def promoted() =>
        return Student(name: self.name, grade: self.grade + 1)
    end
end

def main() =>
    student := Student(name: "Ada", grade: 10)
    promoted := student.promoted()
    return (
        inherited_field: promoted.name,
        inherited_method: promoted.label(),
        grade: promoted.grade
    )
end
