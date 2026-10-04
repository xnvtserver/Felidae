import "db".

class Node
    key(id).
    def id: string.
end

def main() =>
    return db.sync(path: "invalid_sync_link_source.fx").
end
