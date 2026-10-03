# An inner try catches its own error; the outer catch never runs.
def main() =>
    try
        try
            throw(exception: {kind: "inner", message: "inner error"}).
        catch inner then
            handled := inner.message.
        end
        outcome := "outer completed".
    catch outer then
        outcome := outer.message.
    end
    return (handled: handled, outcome: outcome).
end
