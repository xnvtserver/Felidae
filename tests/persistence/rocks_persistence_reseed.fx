class PersistentMetric
    key(id).
    id: string.
    value: number.
end

# A record is a key/value block; field order is not part of its identity.
PersistentMetric(value: 42, id: "metric-1").

def main() =>
    return PersistentMetric.count().
end
