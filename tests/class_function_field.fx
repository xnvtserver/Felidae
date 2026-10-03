class CalculatedNode
    key(id).
    id: string.
    field1: string := "unset".

    def populate() =>
        this.field1 := something().
        return this.
    end
end

# Deliberately declared after the class: module loading must register the
# complete source before main invokes CalculatedNode.populate().
def something() =>
    return "computed".
end

def main() =>
    node := new CalculatedNode(id: "node-1").
    changed := node.populate().
    saved := changed.save().
    stored := CalculatedNode.where(id: "node-1").get(pos: 0).
    return (
        transient: changed.field1,
        stored: stored.field1,
        same_identity: changed == node
    ).
end
