# A fact family may have several direct parents. Queries on either ancestor
# include every descendant once, while direct-parent order remains stable for
# deterministic comparison and explanation paths.

Government(domain: "public")
People(category: "being")
Politician extend Government, People(name: "Ravi", office: "council")
Policeman extend Politician(service: "community")

main() =>
    governments := Fact.all(type: "Government")
    people := Fact.all(type: "People")
    politicians := Fact.all(type: "Politician")
    politician := Fact.first(type: "Politician", field: "name", equals: "Ravi")
    return HierarchySelectionReport(
        government_count: count(governments),
        people_count: count(people),
        politician_count: count(politicians),
        government_members: governments,
        people_members: people,
        politician_public_domain: politician.domain,
        politician_people_category: politician.category
    )
