@mixfix(pattern: "plan {name: string} using {strategy: string} with {budget: number} when {enabled: bool} carrying {tags: array} for {owner: string}")
longPlan(name:string, strategy:string, budget:number, enabled:bool, tags:array, owner:string) =>
    return Plan(name: name, strategy: strategy, budget: budget, enabled: enabled, tags: tags, owner: owner)

@mixfix(pattern: "plan {value: number} using {enabled: bool}")
shortPlan(value:number, enabled:bool) =>
    return Plan(value: value, enabled: enabled)

main() =>
    a := plan "procedure" using "all-data" with 10 when true carrying ["x"] for "owner"
    b := plan 7 using false
    return (a: a, b: b)
