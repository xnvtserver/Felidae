@mixfix(
    pattern: "requiredPositive {value: number}",
    type: "prefix",
    result: number.class,
    precedence: "prefix",
    associativity: "right",
    cardinality: "one",
    effects: "pure",
    visibility: "private"
)
def positiveOnly() =>
    where value > 0
    return value
end

def main() =>
    return requiredPositive -1
end
