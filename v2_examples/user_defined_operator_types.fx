Employee(name: "Ava", role: "general")
Engineer extend Employee(name: "Noah", role: "engineering")
SeniorEngineer extend Engineer(name: "Mia", role: "architecture")

@overload(
    operator: classifyRequirement,
    pattern: "{left} classifyRequirement {right}",
    captures: {left: Employee, right: number},
    result: string,
    cardinality: one,
    effects: pure,
    visibility: private
)
classifyEmployee() =>
    return left.name

main() =>
    return lambda(Employee, employee => employee classifyRequirement 1)
