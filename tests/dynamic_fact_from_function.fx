class School
    name: string
    district: string
    students: number
    active: number
end

make_school(name: string, district: string, students: number) =>
    return School.insert(values: {
        name: name,
        district: district,
        students: students,
        active: 1.0
    })
end

main() =>
    inserted := make_school(
        name: "Function School",
        district: "central",
        students: 240
    )
    matches := School.where(name: "Function School")
    return (
        inserted: inserted,
        count: matches.len(),
        stored: matches.get(position: 0)
    )
end
