class Box
    key(id).
    def id: string.
    def value: number.

    def same() =>
        this.
    end

    def set(value: number) =>
        this.value := value.
        this.
    end
end

def main() =>
    def original := Box(id: "box-1", value: 1).
    def returned := original.same().
    returned.set(value: 2).
    def items := [original].
    def retrieved := items.get(pos: 0).
    retrieved.set(value: 3).
    for item in items then
        item.set(value: 4).
    end
    (
        original: original.value,
        returned: returned.value,
        retrieved: retrieved.value
    ).
end
