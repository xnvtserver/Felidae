# An uncaught throw ends the program and reports the exception message.
def main() =>
    throw(kind: custom, message: "uncaught boom").
    "unreachable".
end
