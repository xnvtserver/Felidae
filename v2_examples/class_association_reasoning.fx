# Classes are compiler-only fact schemas. Mixfix methods produce ordinary
# Association facts that remain queryable through the standard fact DML API.
class Person
    id: number
    name: string
    index(id)
end

class Company
    id: number
    name: string
    index(id)
end

class Association
    left_id: number
    right_id: number
    relation: string
    strength: number
    index(left_id, relation)
    index(right_id, relation)
end

Person(id: 1, name: "Ada")
Company(id: 7, name: "Felidae")

@mixfix(pattern: "{person: Person} works at {company: Company}")
worksAt() =>
    return Association(
        left_id: person.id,
        right_id: company.id,
        relation: "works at",
        strength: 3.421
    )
end

main() =>
    people := fx.cast_<array(Person)>(Person.all())
    companies := fx.cast_<array(Company)>(Company.all())
    person := people.get(position: 0)
    company := companies.get(position: 0)
    return person works at company
end
