InvalidExceptionFlow(reason: reason) =>
    throw(exception: {__type: "Exception", kind: "Failure", message: "Failure", source: "user"})
    reason == "Failure"
    return
