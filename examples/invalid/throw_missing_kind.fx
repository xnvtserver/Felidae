BadException() =>
    throw(exception: {__type: "Exception", message: "missing kind", source: "user"})
    return

main() =>
    BadException()
    return
