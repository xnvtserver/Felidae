# An exception object must carry a string kind.
def main() =>
    throw(exception: {message: "no kind"}).
    return "unreachable".
end
