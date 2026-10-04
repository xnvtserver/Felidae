# Assigning an object into its own field would create a cycle that display,
# clone and persistence cannot traverse, so the assignment is rejected.
class Cell
    key(id).
    def id: string.
    def next: any := 0.

    def tie() =>
        this.next := this.
        return this.
    end
end

def main() =>
    def a := Cell(id: "a").
    return a.tie().
end
