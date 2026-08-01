mark(label: string) =>
    return label

@mark(label: "ready")
decorated() =>
    return "annotation-applied"

main() =>
    return decorated()
