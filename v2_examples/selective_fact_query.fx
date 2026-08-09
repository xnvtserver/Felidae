# A query planner regression: each field has several matches, but only one
# fact satisfies their conjunction.  The runtime must intersect the grounded
# property indexes before it tries fact unification.

Candidate(id: "c01", region: "north", role: "analyst", active: true)
Candidate(id: "c02", region: "north", role: "engineer", active: true)
Candidate(id: "c03", region: "south", role: "analyst", active: true)
Candidate(id: "c04", region: "south", role: "engineer", active: false)
Candidate(id: "c05", region: "east", role: "analyst", active: false)
Candidate(id: "c06", region: "east", role: "engineer", active: true)
Candidate(id: "c07", region: "west", role: "analyst", active: true) as westEmployee
Candidate(id: "c08", region: "west", role: "engineer", active: false) as westEmployee
Candidate(id: "c09", region: "north", role: "analyst", active: false) as northEmployee
Candidate(id: "c10", region: "south", role: "analyst", active: false) as southEmployee

main() =>
    return system.run(value: "? Candidate(region: \"north\", role: \"analyst\", active: true, id: Id)")
