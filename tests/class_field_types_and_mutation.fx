class Node
    key(id).
    id: string.
    label: string.
    enabled: bool := false.
    note: optional<string>.
    scores: list<number> := [].
    coordinates: list<Pair<number, number>> := [Pair(1, 2), Pair(3, 4)].

    def rename(label: string) =>
        this.label := label.
        this.enabled := true.
        return this.
    end
end

class Holder
    key(id).
    id: string.
    item: obj.
    payload: any.
end

def main() =>
    node := Node(id: "n1", label: "before").
    alias := node.
    changed := node.rename(label: "after").
    holder := Holder(id: "h1", item: node, payload: [1, "two", false]).
    saved := node.save().
    stored := Node.where(id: "n1").get(pos: 0).
    return (
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
