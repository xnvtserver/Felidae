School(name: "North", district: "central", active: 1.0).
School(name: "South", district: "west", active: 1.0).
School(name: "Closed", district: "central", active: 0.0).

main() =>
    central_or_west := School.where(district: "central").OrWhere(district: "west")
    active_central := School.where(district: "central").AndWhere(active: 1.0)
    top_one := School.where(active: 1.0).limit(records: 1)
    return (or_count: count(central_or_west), and_count: count(active_central), limited: top_one)
