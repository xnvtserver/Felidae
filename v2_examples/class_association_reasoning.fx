# Classes declare direct-interpreter fact schemas. Mixfix methods produce
# ordinary Association facts queryable through the standard fact API.
class Person
    def id: number.
    def name: string.
    index(id).
end

class Company
    def id: number.
    def name: string.
    index(id).
end

class Association
    def left_id: number.
    def right_id: number.
    def relation: string.
    def strength: number.
    index(left_id, relation).
    index(right_id, relation).
end

def Person(id: 1, name: "Ada").
def Company(id: 7, name: "Felidae").

@mixfix(pattern: '{person: Person} works at {company: Company}')
def worksAt() =>
    Association(
        left_id: person.id,
        right_id: company.id,
        relation: "works at",
        strength: 3.421
    ).
end

def main() =>
    def person := Person.get(pos: 0).
    def company := Company.get(pos: 0).
    person works at company.
end
