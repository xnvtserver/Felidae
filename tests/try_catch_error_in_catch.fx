# An error raised inside a catch body is not swallowed by its own try: it
# propagates to the enclosing try.
def main() =>
    try
        try
            throw(kind: first, message: "original").
        catch inner then
            throw(kind: second, message: "raised while handling").
        end
    catch outer then
        def kind := outer.kind.
        def message := outer.message.
    end
    (kind: kind, message: message).
end
