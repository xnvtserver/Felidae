# Standard result and exception values.
#
# This library deliberately defines no application error kinds. A source file
# chooses its own stable `kind` values and decides how to recover. Recoverable
# operations return Result(ok, value, error); callers inspect `ok` or `error`
# and invoke an ordinary handler method when recovery is appropriate.

def Exception(kind: string, message: string, source: string) => ()
def Result(ok: bool, value: any, error: any) => ()

def exception.ok(value: any) =>
    return {__type: "Result", ok: 1.0, value: value, error: nil}

def exception.failure(kind: string, message: string, source: string) =>
    return {
        __type: "Result",
        ok: 0.0,
        value: nil,
        error: {
            __type: "Exception",
            kind: kind,
            message: message,
            source: source
        }
    }

def exception.from(value: any, error: any) =>
    if error == nil then
        return exception.ok(value: value)
    else
        return {__type: "Result", ok: 0.0, value: value, error: error}
