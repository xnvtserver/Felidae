DivideFailure(error_reason: error_reason) =>
    throw(msg: "DivisionByZero"),
    error_reason == "DivisionByZero".

ProgrammingFailure(error_reason: error_reason) =>
    throw(msg: "ProgrammingError"),
    error_reason == "ProgrammingError".

OtherFailure(error_reason: error_reason) =>
    throw(msg: "UnknownError", out: error_reason),
    error_reason != "DivisionByZero",
    error_reason != "ProgrammingError".

DivideFailureHandler(msg: msg) =>
    HandledFailure(type: "division", msg: msg).

HandledFailure(type: "division", msg: "thrown from module a").

RoutedFailure(msg: msg) =>
    throw(msg: "thrown from module a", target: DivideFailureHandler),
    HandledFailure(type: "division", msg: msg).


main() =>
    divideFailure := DivideFailure(error_reason: "DivisionByZero"),
    programmingFailure := ProgrammingFailure(error_reason: "ProgrammingError"),
    otherFailure := OtherFailure(error_reason: "SomeOtherError"),
    routedFailure := RoutedFailure(msg: "thrown from module a"),
    return (
        divide: divideFailure,
        programming: programmingFailure,
        other: otherFailure,
        routed: routedFailure
    ).