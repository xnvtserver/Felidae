# User and library exceptions use the same typed value contract. The source
# identifies who raised it; kind is the stable programmatic discriminator.

exceptionActions.handle(exception: {__type: "Exception", kind: "DivisionByZero", message: message, source: source}) =>
    return true

exceptionActions.handle(exception: {__type: "Exception", kind: "ProgrammingError", message: message, source: source}) =>
    return true

exceptionActions.handle(exception: {__type: "Exception", kind: "UnknownError", message: message, source: source}) =>
    return true

exceptionActions.handle(exception: {__type: "Exception", kind: "ModuleFailure", message: message, source: source}) =>
    return true

DivideFailure(error_reason: error_reason) =>
    exception := {
        __type: "Exception",
        kind: "DivisionByZero",
        message: "Cannot divide by zero",
        source: "library"
    }
    throw(exception: exception, target: exceptionActions::handle)
    return (error_reason: exception.kind)

ProgrammingFailure(error_reason: error_reason) =>
    exception := {
        __type: "Exception",
        kind: "ProgrammingError",
        message: "Invalid program state",
        source: "user"
    }
    throw(exception: exception, target: exceptionActions::handle)
    return (error_reason: exception.kind)

OtherFailure(error_reason: error_reason) =>
    exception := {
        __type: "Exception",
        kind: "UnknownError",
        message: "Unknown failure",
        source: "user"
    }
    throw(exception: exception, target: exceptionActions::handle)
    return (error_reason: exception.kind)

RoutedFailure(msg: msg) =>
    exception := {
        __type: "Exception",
        kind: "ModuleFailure",
        message: "thrown from module a",
        source: "library"
    }
    throw(exception: exception, target: exceptionActions::handle)
    return (msg: exception.message)

main() =>
    return (
        divide: DivideFailure(error_reason: "DivisionByZero"),
        programming: ProgrammingFailure(error_reason: "ProgrammingError"),
        other: OtherFailure(error_reason: "UnknownError"),
        routed: RoutedFailure(msg: "thrown from module a")
    )
