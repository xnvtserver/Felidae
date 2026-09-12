// Receiver calls reuse the existing array operations.
// push returns a new array, so retain its result explicitly.
main() =>
    people:obj = array()
    people := people.push(value: "Ada")
    people := people.push(value: "Grace")
    return (first: people.get(position: 0), count: people.len())
end
