class CalculatedNode
    key(id).
    def id: string.
    def field1: string := "unset".

    def populate() =>
        this.field1 := something().
        this.
    end
end

# Deliberately declared after the class: module loading must register the
# complete source before main invokes CalculatedNode.populate().
def something() =>
    "computed".
end

def main() =>
    def node := new CalculatedNode(id: "node-1").
    def changed := node.populate().
    def saved := changed.save().
    def stored := CalculatedNode.where(id: "node-1").get(pos: 0).
    (
        transient: changed.field1,
        stored: stored.field1,
        same_identity: changed = node
    ).
end
