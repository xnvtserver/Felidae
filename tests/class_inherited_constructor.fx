class Person
    name: string

    label() =>
        return self.name
    end
end

class Student extends Person
    grade: number

    promoted() =>
        return Student(name: self.name, grade: self.grade + 1)
    end
end

main() =>
    student := Student(name: "Ada", grade: 10)
    promoted := student.promoted()
    return (
        inherited_field: promoted.name,
        inherited_method: promoted.label(),
        grade: promoted.grade
    )
end
