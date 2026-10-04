class Visit
    key(id).
    def id: number.
    def source: string.
end

def main() =>
    for i in range(0, 5) then
        if i == 2 then
            continue.
        end
        Visit.insert(values: {id: i, source: "for"}).
    end

    while Visit.count() < 6 then
        def next := Visit.count().
        Visit.insert(values: {id: next + 10, source: "while"}).
    end

    switch "middle"
    case "first" then
        Visit.insert(values: {id: 20, source: "first"}).
    case "middle" then
        Visit.insert(values: {id: 21, source: "middle"}).
    case "fallthrough" then
        Visit.insert(values: {id: 22, source: "fallthrough"}).
        break.
    default then
        Visit.insert(values: {id: 23, source: "default"}).
    end

    def selected := Visit.where(source: "for").
    for row in selected then
        # Iteration over a fact selection is valid; field access also verifies
        # that the loop receives records rather than an opaque cursor handle.
        row.id >= 0.
    end
    return (
        for_count: selected.count(),
        while_count: Visit.where(source: "while").count(),
        middle_count: Visit.where(source: "middle").count(),
        fallthrough_count: Visit.where(source: "fallthrough").count(),
        default_count: Visit.where(source: "default").count()
    ).
end
