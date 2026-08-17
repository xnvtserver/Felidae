# Same leading/trailing anchors, selected from typed captures and lowered to
# ordinary verified Call IR.  No AST reaches the Form VM.

@mixfix(pattern: "plan {name: string} using {strategy: string}")
stringPlan(name: string, strategy: string) =>
    return name

@mixfix(pattern: "plan {value: number} using {enabled: bool}")
numberPlan(value: number, enabled: bool) =>
    return value

main() =>
    text := plan "procedure" using "all-data"
    numeric := plan 7 using true
    return (text: text, numeric: numeric)
