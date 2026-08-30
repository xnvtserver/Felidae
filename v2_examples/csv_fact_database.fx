# CSV enters the VM as native School facts. Concrete fact-type queries use the
# same retained knowledge store; DML writes owned CSV facts back automatically.

import "csv"

School(name: "Existing School", district: "north", students: 190.0, active: 1.0)

main() =>
    csvData := file.readFile(file: "datasets/examples/schools.csv")
    imported := csv.toFacts(data: csvData, type: "School", source: "build/runtime/schools.csv")
    allSchools := School.all()
    activeCentral := School.where(district: "central", active: 1.0)
    allCount := School.count()
    activeCentralCount := count(data: activeCentral)
    updated := School.where(name: "North School").update(values: {active: 0.0})
    inserted := School.insert(values: {name: "Temporary School", district: "test", students: 1.0, active: 1.0})
    deleted := School.where(name: "Temporary School").delete()
    return (
        imported: imported,
        all_schools: allSchools,
        active_central: activeCentral,
        all_count: allCount,
        active_central_count: activeCentralCount,
        updated: updated,
        inserted: inserted,
        deleted: deleted
    )
end
