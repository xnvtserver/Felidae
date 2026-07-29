import "fact"

main() =>
    team := Team(
        lead: Person(name: "Ravi"),
        members: [Person(name: "Leela")],
        profile: Profile(region: "south")
    )
    nearest := Fact.nearestSubfacts(input: team, maximumDepth: 2)
    direct := Fact.nearestSubfacts(input: team, maximumDepth: 1)
    return NestedFactNeighborReport(nearest: nearest, direct: direct)
