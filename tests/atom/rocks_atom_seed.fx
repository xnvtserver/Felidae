class AtomRecord
    key(id).
    index(state).
    def id: string.
    def state: any.
    def values: any.
    def metadata: any.
    def term: any.
end

def AtomRecord(
    id: "a1",
    state: active,
    values: [pending, approved],
    metadata: {state: active},
    term: Pair(active, approved)
).
def AtomRecord(
    id: "a2",
    state: "active",
    values: ["pending", "approved"],
    metadata: {state: "active"},
    term: Pair("active", "approved")
).

def main() =>
    (
        atoms: AtomRecord.where(state: active).count(),
        strings: AtomRecord.where(state: "active").count(),
        distinct: active != "active"
    ).
end
