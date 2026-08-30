import ("array", "system")

# Capitalized names trigger Felidae's implicit fact-construction heuristic
# (capitalized name + named args => build a fact literal) instead of calling
# a same-named declared procedure -- a real, silent language sharp edge with
# no error/warning. Lowercase procedure names here avoid that ambiguity;
# see v2_examples/where_guard_chain.fx for the same fix on the same issue.
increment(value: number) =>
    return value + 1

double(value: number) =>
    return value * 2

eligible(score: number, active: bool) =>
    where score >= 70
    where active == true
    return true
else
    return false

main() =>
    point := {x: 3, y: 4}
    record := {owner: {name: "Ava"}, scores: [10, 20, 30]}
    # lambda(...) is fact-query sugar over a declared fact type (see
    # FactQueryNormalizer) -- there is no generic array-map lambda over an
    # arbitrary array like record.scores, so indexing it directly here.
    first := array.get(data: record.scores, position: 0)
    piped := increment(value: first)
        then double(value: system.result)
    return (
        precedence: 2 + 3 * 4,
        grouped: (2 + 3) * 4,
        unary: -(point.x + point.y),
        ordered: point.y > point.x,
        unequal: point.x != point.y,
        eligible: eligible(score: 75, active: true),
        member: record.owner.name,
        pipeline: piped
    )
