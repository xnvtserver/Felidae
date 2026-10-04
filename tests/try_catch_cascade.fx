# Cascade: an error raised inside a catch body goes to the next catch. Here e
# relays to k, k relays to p, and p finally handles it.
def main() =>
    try
        throw(exception: {kind: "a", message: "from try"}).
    catch e then
        throw(exception: {kind: "b", message: e.message}).
    catch k then
        throw(exception: {kind: "c", message: k.kind}).
    catch p then
        def got_kind := p.kind.
        def got_message := p.message.
    end
    return (kind: got_kind, message: got_message).
end
