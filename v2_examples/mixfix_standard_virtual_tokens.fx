# `plan`, `using`, and every literal anchor are registered as virtual lexer
# tokens by the annotation. They remain ordinary identifiers outside this
# expression syntax.
@mixfix(
    pattern: "plan {name: string} using {strategy: string}"
)
planer(name: string, strategy: string) =>
    assertion := strategy == "direct"
    if assertion then
        return Plan(name: name, strategy: strategy, asserted: 1.0)
    else
        return Plan(name: name, strategy: strategy, asserted: 0.0)

main() =>
    plan := "ordinary identifier"
    direct := plan "Felidae" using "direct"
    deferred := plan "Felidae" using "deferred"
    return (ordinary: plan, direct: direct, deferred: deferred)
