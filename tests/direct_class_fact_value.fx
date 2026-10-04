class Person
    def id: number.
    def name: string.
end

def Person(id: 1, name: "Ada").

def main() =>
    def person := Person.get(pos: 0).
    def people := Person.all().
    def from_array := people.get(pos: 0).
    return (person: person, from_array: from_array, name: person.get(key: "name")).
end
