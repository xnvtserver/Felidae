# A catch body can rethrow the exception it received; the outer catch sees the
# same kind and message.
def main() =>
    try
        try
            throw(exception: {kind: "disk", message: "full"}).
        catch inner then
            throw(exception: inner).
        end
    catch outer then
        def kind := outer.kind.
        def message := outer.message.
    end
    return (kind: kind, message: message).
end
