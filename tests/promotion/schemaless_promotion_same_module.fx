def Metric(id: "metric-1", value: 10).

class Metric
    key(id).
    index(value).
    def id: string.
    def value: number.

    def doubled() =>
        return this.value * 2.
    end
end

def main() =>
    def stored := Metric.where(value: 10).get(pos: 0).
    return (count: Metric.count(), doubled: stored.doubled()).
end
