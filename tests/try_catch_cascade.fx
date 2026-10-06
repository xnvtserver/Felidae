# Cascade: an error raised inside a catch body goes to the next catch. Here e
# relays to k, k relays to p, and p finally handles it.
def main() =>
    try
        throw(kind: a, message: "from try").
    catch e then
        throw(kind: b, message: e.message).
    catch k then
        throw(kind: c, message: "b").
    catch p then
        def got_kind := p.kind.
        def got_message := p.message.
    end
    (kind: got_kind, message: got_message).
end
