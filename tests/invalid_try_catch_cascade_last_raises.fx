# An error raised by the last catch has nowhere left to cascade to and ends
# the program with that error.
def main() =>
    try
        throw(exception: {kind: "a", message: "from try"}).
    catch e then
        throw(exception: {kind: "b", message: "from first catch"}).
    catch k then
        throw(exception: {kind: "c", message: "last catch failed"}).
    end
    return "unreachable".
end
