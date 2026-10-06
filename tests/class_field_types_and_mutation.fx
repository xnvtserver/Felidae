class Node
    key(id).
    def id: string.
    def label: string.
    def enabled: bool := false.
    def note: optional<string>.
    def scores: list<number> := [].
    def coordinates: list<Pair<number, number>> := [Pair(1, 2), Pair(3, 4)].

    def rename(label: string) =>
        this.label := label.
        this.enabled := true.
        this.
    end
end

class Holder
    key(id).
    def id: string.
    def item: obj.
    def payload: any.
end

def main() =>
    def node := Node(id: "n1", label: "before").
    def alias := node.
    def changed := node.rename(label: "after").
    def holder := Holder(id: "h1", item: node, payload: [1, "two", false]).
    def saved := node.save().
    def stored := Node.where(id: "n1").get(pos: 0).
    (
        label: alias.label,
        changed_label: changed.label,
        enabled: node.enabled,
        note: node.note,
        score_count: node.scores.len(),
        pair_count: node.coordinates.len(),
        held_label: holder.item.label,
        stored_pair_count: stored.coordinates.len()
    ).
end
