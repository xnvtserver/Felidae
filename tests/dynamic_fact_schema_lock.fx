Metric(id: "first", value: 10)
Metric(id: "second", value: 20)

def main() =>
    transient := new Metric(id: "third", value: 30),
    return (stored: Metric.count(), transient: transient.value)
end
