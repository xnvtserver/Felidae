import ("json", "csv", "file")

RawPeopleCsv := "name,role,office\nAlice,Engineer,SEA\nBob,Manager,LAX\nCarol,Engineer,SEA\n"

People := csv.toFacts(data: RawPeopleCsv, type: "PersonRow")

SeaEngineers := lambda(People, person => person.office == "SEA")

ProjectedSeaEngineers := lambda(
    SeaEngineers,
    person => {
        employee: {
            name: person.name,
            role: person.role
        },
        source: "csv",
        tags: ["sea", "engineering"]
    }
)

main() =>
    facts := csv.toFelidaeFacts(data: lambda(SeaEngineers, row => {name: row.name, office: row.office}), type: "SeaEmployee")
    writeStatus := file.writeFile(path: "build/data_structure_stress_generated.fx", data: facts, mode: "write")
    return (
        count: count(ProjectedSeaEngineers),
        generated: writeStatus,
        projected: ProjectedSeaEngineers,
        known_gap: "expression array:get is not evaluated yet"
    )
