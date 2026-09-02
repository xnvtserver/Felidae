# A fact-only .fx file is DDL data linked by the compiler. Imported rows keep
# source ownership, so conditional insert/update/delete operations can persist
# them automatically. This example is read-only to keep the checked-in dataset
# deterministic; copy the database under build/ before experimenting with DML.
import "../datasets/examples/schools.fx"

main() =>
    active := School.where(active: 1.0).limit(records: 100)
    return (
        rows: active,
        count: count(data: active),
        students: School.sum(field: "students", match: {active: 1.0})
    )
end
