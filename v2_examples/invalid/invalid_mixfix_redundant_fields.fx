@mixfix(
    operator: redundantPlan,
    pattern: "plan {name: string} using {strategy: string}",
    captures: {name: string, strategy: string}
)
planer() =>
    return Plan(name: name, strategy: strategy)
