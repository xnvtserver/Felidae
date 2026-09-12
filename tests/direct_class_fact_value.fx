class Person
    id: number
    name: string
end

Person(id: 1, name: "Ada")

def main() =>
    person := Person.get(pos: 0)
    people := Person.all()
    from_array := people.get(pos: 0)
    return (person: person, from_array: from_array, name: person.get(key: "name"))
end
