@mixfix(
    operator: redundantPlan.function,
    pattern: "plan {name: string} using {strategy: string}",
    captures: {name: string, strategy: string}
)
def planer() =>
    return Plan(name: name, strategy: strategy)
end
