# Metric was never declared as a class, so its facts are not validated: a
# different value type and a different field set are both accepted. Only the
# key field (id, the first field of the first constructor) is contracted.
Metric(id: "m2", value: "any type is accepted").
Metric(id: "m3", unit: "ms").

def main() =>
    return Metric.count().
end
