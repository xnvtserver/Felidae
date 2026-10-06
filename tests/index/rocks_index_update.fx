def main() =>
    def changed := IndexedMetric.where(id: "m1")
        .update(values: {active: false}).
    (changed: changed, active: IndexedMetric.where(active: true).len()).
end
