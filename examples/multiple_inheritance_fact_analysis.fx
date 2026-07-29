import "fact"

Government(domain: "public")
People(category: "being")
Politician extend Government, People(name: "Ravi", office: "council")
Policeman extend Politician(name: "Asha", service: "community")

main() =>
    people := Fact.first(type: "People", field: "category", equals: "being")
    politician := Fact.first(type: "Politician", field: "name", equals: "Ravi")
    policeman := Fact.first(type: "Policeman", field: "name", equals: "Asha")
    return MultipleInheritanceAnalysis(
        people_is_ancestor: fact.isAncestor(ancestor: people, descendant: policeman),
        government_is_ancestor: fact.isAncestor(ancestor: Fact.first(type: "Government", field: "domain", equals: "public"), descendant: policeman),
        people_is_direct_parent: fact.directRelation(parent: people, child: politician)
    )
