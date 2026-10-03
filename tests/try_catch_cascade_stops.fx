# The cascade stops at the first catch that completes: the later catch body
# never runs, so its insert does not happen.
class Item
    key(id).
    id: string.
end

def main() =>
    try
        throw(exception: {kind: "a", message: "from try"}).
    catch e then
        handled := e.message.
    catch k then
        extra := Item.insert(values: {id: "never"}).
    end
    return (handled: handled, items: Item.count()).
end
