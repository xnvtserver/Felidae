class IndexedMetric
    key(id).
    index(active).
    id: string.
    active: bool.
end

IndexedMetric(id: "m1", active: true).
IndexedMetric(id: "m2", active: true).

def main() =>
    return IndexedMetric.where(active: true).len().
end
