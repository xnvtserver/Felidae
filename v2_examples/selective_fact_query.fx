# A query planner regression: each field has several matches, but only one
# fact satisfies their conjunction.  The runtime must intersect the grounded
# property indexes before it tries fact unification.

Candidate(id: "c01", region: "north", role: "analyst", active: 1.0)
Candidate(id: "c02", region: "north", role: "engineer", active: 1.0)
Candidate(id: "c03", region: "south", role: "analyst", active: 1.0)
Candidate(id: "c04", region: "south", role: "engineer", active: 0.0)
Candidate(id: "c05", region: "east", role: "analyst", active: 0.0)
Candidate(id: "c06", region: "east", role: "engineer", active: 1.0)
Candidate(id: "c07", region: "west", role: "analyst", active: 1.0)
Candidate(id: "c08", region: "west", role: "engineer", active: 0.0)
Candidate(id: "c09", region: "north", role: "analyst", active: 0.0)
Candidate(id: "c10", region: "south", role: "analyst", active: 0.0)

main() =>
    matches := Candidate.select(region: "north", role: "analyst", active: 1.0)
    return (matches: matches, count: count(data: matches))
