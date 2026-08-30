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
    updated := School.update(match: {name: "North"}, values: {active: 0.0})
    return (
        imported: imported,
        all_schools: allSchools,
        active_central: activeCentral,
        all_count: allCount,
        active_central_count: activeCentralCount,
        updated: updated
    )
end
