class IndexedMetric
    key(id).
    index(active).
    def id: string.
    def active: bool.
end

def IndexedMetric(id: "m1", active: true).
def IndexedMetric(id: "m2", active: true).

def main() =>
    return IndexedMetric.where(active: true).len().
end
