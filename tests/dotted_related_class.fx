class Person
    key(id).
    def id: string.
end

class School.Student extends Person
    def grade: number.
end

def main() =>
    def student := new School.Student(id: "student-1", grade: 10).
    return (type: type(student), id: student.id, grade: student.grade).
end
