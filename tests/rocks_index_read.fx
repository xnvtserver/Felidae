def main() =>
    return (
        active: IndexedMetric.where(active: true).len(),
        inactive: IndexedMetric.where(active: false).len()
    )
end
