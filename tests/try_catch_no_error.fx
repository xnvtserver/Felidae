# Without an error the catch branch does not run, and bindings made in the try
# body stay visible afterwards.
def main() =>
    try
        def outcome := "completed".
    catch e then
        def outcome := e.message.
    end
    outcome.
end
