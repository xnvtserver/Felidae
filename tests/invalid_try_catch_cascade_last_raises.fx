# An error raised by the last catch has nowhere left to cascade to and ends
# the program with that error.
def main() =>
    try
        throw(kind: a, message: "from try").
    catch e then
        throw(kind: b, message: "from first catch").
    catch k then
        throw(kind: c, message: "last catch failed").
    end
    "unreachable".
end
