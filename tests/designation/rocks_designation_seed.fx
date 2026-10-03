class DesignatedNode
    key(id).
    id: string.
end

DesignatedNode(id: "node-1") as preferred.

def main() =>
    return preferred.count().
end
