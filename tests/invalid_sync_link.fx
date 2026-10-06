import "db".

class Node
    key(id).
    def id: string.
end

def main() =>
    db.sync(path: "invalid_sync_link_source.fx").
end
