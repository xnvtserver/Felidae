P() =>
    not Q()
    return

Q() =>
    not P()
    return

main() =>
    P()
    return "unreachable"
