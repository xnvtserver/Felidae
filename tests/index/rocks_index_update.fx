def main() =>
    changed := IndexedMetric.where(id: "m1")
        .update(values: {active: false}).
    return (changed: changed, active: IndexedMetric.where(active: true).len()).
end
