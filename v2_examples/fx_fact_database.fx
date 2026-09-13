# A fact-only .fx file is loaded directly into FactMemory. This example is
# read-only to keep the checked-in dataset deterministic.
import "../datasets/examples/schools.fx"

def main() =>
    active := School.where(active: 1.0).limit(records: 100)
    return (
        rows: active,
        count: count(data: active),
        students: School.sum(field: "students", match: {active: 1.0})
    )
end
