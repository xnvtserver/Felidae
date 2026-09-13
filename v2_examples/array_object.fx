# Receiver calls reuse the existing array operations.
# push returns a new array, so each step needs its own binding - `:=` never
# reassigns an existing name in Felidae.
def main() =>
    empty := []
    withAda := empty.push(value: "Ada")
    people := withAda.push(value: "Grace")
    return (first: people.get(position: 0), count: people.len())
end
