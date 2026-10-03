class Person
    key(id).
    id: string.
end

class School.Student extends Person
    grade: number.
end

def main() =>
    student := new School.Student(id: "student-1", grade: 10).
    return (type: type(student), id: student.id, grade: student.grade).
end
