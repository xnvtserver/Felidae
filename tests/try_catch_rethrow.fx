# A catch body can rethrow the exception it received; the outer catch sees the
# same kind and message.
def main() =>
    try
        try
            throw(kind: disk, message: "full").
        catch inner then
            throw(kind: inner.kind, message: inner.message).
        end
    catch outer then
        def kind := outer.kind.
        def message := outer.message.
    end
    (kind: kind, message: message).
end
