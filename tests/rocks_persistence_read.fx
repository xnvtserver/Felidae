class PersistentMetric
    key(id)
    id: string
    value: number
end

def main() =>
    return PersistentMetric.where(id: "metric-1").get(pos: 0).value
end
