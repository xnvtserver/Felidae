def School(name: "North", active: 1.0).

def main() =>
    def inserted := School.insert(values: {name: "Temporary", active: 1.0}).
    def updated := School.where(name: "North").update(values: {active: 0.0}).
    def deleted := School.where(name: "Temporary").delete().
    return (inserted: inserted, updated: updated, deleted: deleted, rows: School.all()).
end
