class IndexedReading
    key(id)
    index(active)
    id: string
    active: bool
    category: string
end

IndexedReading(id: "a", active: true, category: "discard")
IndexedReading(id: "b", active: true, category: "keep")

def main() =>
    selected := IndexedReading.where(active: true, category: "keep")
    first := selected.limit(1).get(0)
    return (count: selected.count(), id: first.id)
end
