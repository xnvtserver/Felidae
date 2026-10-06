# An inner try catches its own error; the outer catch never runs.
def main() =>
    try
        try
            throw(kind: inner, message: "inner error").
        catch inner then
            def handled := inner.message.
        end
        def outcome := "outer completed".
    catch outer then
        def outcome := outer.message.
    end
    (handled: handled, outcome: outcome).
end
