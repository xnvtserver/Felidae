# Semantic designations are metadata on existing fact instances. They are
# indexed for query planning and never appear as fields in the returned fact.

Candidate(id: "e01", region: "west", role: "analyst", salary: 25000, active: true) as westEmployee, analyst
# This is the same fact, not a second row: designation metadata is merged.
Candidate(id: "e01", region: "west", role: "analyst", salary: 25000, active: true) as audited
Candidate(id: "e02", region: "west", role: "engineer", salary: 18000, active: false) as westEmployee
Candidate(id: "e03", region: "north", role: "analyst", salary: 30000, active: true) as northEmployee, analyst
Manager extend Candidate(id: "m01", region: "west", role: "manager", salary: 40000, active: true) as westEmployee
Contractor(id: "x01", region: "west", active: true) as westEmployee

main() =>
    west := westEmployee
    runtimeWest := system.run(value: "westEmployee")
    rich := westEmployee.salary > 20000
    richActive := westEmployee.salary > 20000 and active == true
    audited := audited
    allWestRows := lambda(west, row => row)
    richRows := lambda(rich, row => row)
    richActiveRows := lambda(richActive, row => row)
    auditedRows := lambda(audited, row => row)
    typed := system.run(value: "? Candidate as westEmployee")
    return (
        allWest: allWestRows,
        selection: west,
        runtimeSelection: runtimeWest,
        rich: richRows,
        richSelection: rich,
        richActive: richActiveRows,
        audited: auditedRows,
        typed: typed
    )
