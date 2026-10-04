# throw raises an exception object {kind, message}; catch receives it intact.
def main() =>
    try
        throw(exception: {kind: "custom", message: "boom"}).
    catch e then
        def caught_kind := e.kind.
        def caught_message := e.message.
    end
    return (kind: caught_kind, message: caught_message).
end
