class PersistentMetric
    key(id).
    def id: string.
    def value: number.
end

def main() =>
    return PersistentMetric.where(id: "metric-1").get(pos: 0).value.
end
