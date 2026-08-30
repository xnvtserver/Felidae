@mixfix(pattern: "plan {name: string} using {strategy: string}")
stringPlan(name:string, strategy:string) =>
    return Plan(name: name, strategy: strategy)

@mixfix(pattern: "plan {value: number} using {enabled: bool}")
numberPlan(value:number, enabled:bool) =>
    return Plan(value: value, enabled: enabled)

main() =>
    text := plan "procedure" using "all-data"
    numeric := plan 7 using 1.0
    return (text: text, numeric: numeric)
