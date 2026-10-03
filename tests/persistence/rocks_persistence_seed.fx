class PersistentMetric
    key(id).
    id: string.
    value: number.
end

PersistentMetric(id: "metric-1", value: 42).

def main() =>
    return PersistentMetric.count().
end
