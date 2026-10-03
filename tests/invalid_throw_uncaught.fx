# An uncaught throw ends the program and reports the exception message.
def main() =>
    throw(exception: {kind: "custom", message: "uncaught boom"}).
    return "unreachable".
end
