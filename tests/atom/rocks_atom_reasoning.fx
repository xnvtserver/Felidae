def active_atom(id: record_id) =>
    def AtomRecord(id: record_id, state: state).
    state = active.
end

def active_string(id: record_id) =>
    def AtomRecord(id: record_id, state: state).
    state = "active".
end

def main() =>
    def atom_match := reasoning.prove(query: active_atom(id: "a1")).
    def atom_rejects_string := reasoning.prove(query: active_atom(id: "a2")).
    def string_rejects_atom := reasoning.prove(query: active_string(id: "a1")).
    def string_match := reasoning.prove(query: active_string(id: "a2")).
    return (
        atom: atom_match.truth_status,
        distinct: atom_rejects_string.truth_status,
        reverse_distinct: string_rejects_atom.truth_status,
        string: string_match.truth_status
    ).
end
