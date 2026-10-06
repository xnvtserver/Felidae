class IndexedReading
    key(id).
    index(active).
    def id: string.
    def active: bool.
    def category: string.
end

def IndexedReading(id: "a", active: true, category: "discard").
def IndexedReading(id: "b", active: true, category: "keep").

def main() =>
    def selected := IndexedReading.where(active: true, category: "keep").
    def first := selected.limit(1).get(0).
    (count: selected.count(), id: first.id).
end
