class Person
    def name: string.

    def label() =>
        return this.name.
    end
end

class Student extends Person
    def grade: number.

    def promoted() =>
        return Student(name: this.name, grade: this.grade + 1).
    end
end

def main() =>
    def student := Student(name: "Ada", grade: 10).
    def promoted := student.promoted().
    return (
        inherited_field: promoted.name,
        inherited_method: promoted.label(),
        grade: promoted.grade
    ).
end
