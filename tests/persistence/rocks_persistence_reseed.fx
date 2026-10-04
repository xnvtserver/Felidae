class PersistentMetric
    key(id).
    def id: string.
    def value: number.
end

# A record is a key/value block; field order is not part of its identity.
def PersistentMetric(value: 42, id: "metric-1").

def main() =>
    return PersistentMetric.count().
end
