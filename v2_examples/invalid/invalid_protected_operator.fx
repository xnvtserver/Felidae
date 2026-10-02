@mixfix(
    pattern: "{left: any} == {right: any}",
    result: bool.class,
    precedence: "ordering",
    associativity: "none",
    cardinality: "one",
    effects: "pure",
    visibility: "private"
)
def replaceEquality() =>
    return 1.0
end

def main() =>
    return 1 == 2
end
