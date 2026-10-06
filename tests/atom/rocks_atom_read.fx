def main() =>
    # Intern unrelated symbols before reading the persisted atom. Durable atom
    # identity is its tagged UTF-8 spelling, never its process-local SymbolId.
    def noise := pending.
    def record := AtomRecord.where(id: "a1").first().
    (
        atoms: AtomRecord.where(state: active).count(),
        strings: AtomRecord.where(state: "active").count(),
        noise: noise,
        persisted_atom: is_atom(record.state),
        nested_list: record.values.get(pos: 1),
        nested_map: record.metadata.state,
        nested_term: record.term
    ).
end
