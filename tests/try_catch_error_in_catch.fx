# An error raised inside a catch body is not swallowed by its own try: it
# propagates to the enclosing try.
def main() =>
    try
        try
            throw(exception: {kind: "first", message: "original"}).
        catch inner then
            throw(exception: {kind: "second", message: "raised while handling"}).
        end
    catch outer then
        def kind := outer.kind.
        def message := outer.message.
    end
    return (kind: kind, message: message).
end
