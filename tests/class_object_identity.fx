class Box
    key(id)
    id: string
    value: number

    def same() =>
        return this
    end

    def set(value: number) =>
        this.value := value
        return this
    end
end

def main() =>
    original := Box(id: "box-1", value: 1)
    returned := original.same()
    returned.set(value: 2)
    items := [original]
    retrieved := items.get(pos: 0)
    retrieved.set(value: 3)
    for item in items then
        item.set(value: 4)
    end
    return (
        original: original.value,
        returned: returned.value,
        retrieved: retrieved.value
    )
end
