class Person
    name: string
    index(name)
end

class Student extends Person
    grade: number
end

Student(name: "Ada", grade: 10)

increment(value: number) =>
    return value + 1
end

main() =>
    return (count: Person.count(), answer: increment(value: 41))
end
