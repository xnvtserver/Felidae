import "smoke".

main() =>
    smoke.fail(message: "expected native failure"),
    return "unreachable".
