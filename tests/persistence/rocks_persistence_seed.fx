class PersistentMetric
    key(id).
    def id: string.
    def value: number.
end

def PersistentMetric(id: "metric-1", value: 42).

def main() =>
    return PersistentMetric.count().
end
