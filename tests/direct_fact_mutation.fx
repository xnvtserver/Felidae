School(name: "North", active: 1.0).

main() =>
    inserted := School.insert(values: {name: "Temporary", active: 1.0})
    updated := School.where(name: "North").update(values: {active: 0.0})
    deleted := School.where(name: "Temporary").delete()
    return (inserted: inserted, updated: updated, deleted: deleted, rows: School.all())
