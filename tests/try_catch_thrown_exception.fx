# throw raises an atom-tagged exception; catch receives its kind and message.
def main() =>
    try
        throw(kind: custom, message: "boom").
    catch e then
        def caught_kind := e.kind.
        def caught_message := e.message.
    end
    (kind: caught_kind, message: caught_message).
end
