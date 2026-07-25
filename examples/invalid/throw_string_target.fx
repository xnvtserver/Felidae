invalidActions.handle(exception: any) =>
    return true

BadTarget() =>
    throw(
        exception: {__type: "Exception", kind: "Failure", message: "failure", source: "user"},
        target: "invalidActions::handle"
    )
    return

main() =>
    BadTarget()
    return
