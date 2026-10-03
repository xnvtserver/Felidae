# Without an error the catch branch does not run, and bindings made in the try
# body stay visible afterwards.
def main() =>
    try
        outcome := "completed".
    catch e then
        outcome := e.message.
    end
    return outcome.
end
