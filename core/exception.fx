# Standard result and exception values.
#
# This library deliberately defines no application error kinds. A source file
# chooses its own stable `kind` values and decides how to recover. Recoverable
# operations return Result(ok, value, error); callers inspect `ok` or `error`
# and invoke an ordinary handler method when recovery is appropriate.

def Exception(kind: atom, message: string, source: string) => ()
end
def Result(ok: bool, value: any, error: any) => ()
end

def exception.ok(value: any) =>
    {__type: "Result", ok: 1.0, value: value, error: nil}.
end

def exception.failure(kind: atom, message: string, source: string) =>
    {
        __type: "Result",
        ok: 0.0,
        value: nil,
        error: {
            __type: "Exception",
            kind: kind,
            message: message,
            source: source
        }
    }.
end

def exception.from(value: any, error: any) =>
    error = nil then exception.ok(value: value)
    else {__type: "Result", ok: 0.0, value: value, error: error}.
end
