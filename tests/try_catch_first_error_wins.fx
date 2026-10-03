# The first error ends the try body: later goals do not run, so the second
# insert never happens.
class Item
    key(id).
    id: string.
end

def main() =>
    try
        first := Item.insert(values: {id: "before"}).
        throw(exception: {kind: "stop", message: "first error"}).
        second := Item.insert(values: {id: "after"}).
        throw(exception: {kind: "stop", message: "second error"}).
    catch e then
        reason := e.message.
    end
    return (reason: reason, items: Item.count()).
end
