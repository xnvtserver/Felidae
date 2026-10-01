# Receiver calls reuse the immutable list operations.
def main() =>
    people: list<string> := ["Ada", "Grace"]
    return (first: people.get(position: 0), count: people.len())
end
