# Six heterogeneous captures.  The anchors deliberately alternate with
# expressions so every capture boundary must be recognized by the parser.

routeSummary(name: string, count: number, enabled: bool, tags: array, owner: string) =>
    return name

@mixfix(
    pattern: "plan {name: string} using {strategy: string} with {budget: number} when {enabled: bool} carrying {tags: array} for {owner: string}"
)
planRouteValue() =>
    inspected := inspect name with budget flags enabled carrying tags toward owner
    return inspected

@mixfix(
    pattern: "plan {value: number} using {assertion: bool}"
)
planer() =>
    if assertion then
        return Plan(name: value, strategy: "accepted")
    else
        return Plan(name: value, strategy: "rejected")

@mixfix(
    pattern: "inspect {name: string} with {count: number} flags {enabled: bool} carrying {tags: array} toward {owner: string}"
)
inspectRouteValue() =>
    return routeSummary(
        name: name,
        count: count,
        enabled: enabled,
        tags: tags,
        owner: owner
    )

main() =>
    route := plan "felidae" using "balanced" with (2 + 3 * 4) when (true and not false) carrying ["facts", "rules", "evidence"] for "runtime"
    inspected := inspect "felidae" with (1 + 2) flags (5 > 3) carrying ["ancestry", "confidence"] toward "reasoner"
    nested := plan ("felidae") using ("deep") with ((1 + 2) * (3 + 4)) when ((2 < 3) and true) carrying ["a", "b"] for ("engine")
    decision := plan 7 using true
    return (route: route, inspected: inspected, nested: nested, decision: decision)
