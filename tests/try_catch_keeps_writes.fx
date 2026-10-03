# Store writes made in the try body before the failure are kept; the catch
# branch decides what to do about them.
class Item
    key(id).
    id: string.
end

def main() =>
    try
        inserted := Item.insert(values: {id: "i1"}).
        throw(exception: {kind: "late", message: "after the write"}).
    catch e then
        reason := e.message.
    end
    return (reason: reason, items: Item.count()).
end
