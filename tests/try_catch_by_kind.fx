# Different exceptions are told apart by kind inside one catch body.
def classify(kind: string) =>
    try
        kind = "missing" then throw(kind: not_found, message: "no such row")
        else kind = "bad" then throw(kind: invalid, message: "bad input")
        else true.
        def verdict := "ok".
    catch e then
        def verdict := e.kind = not_found then "handled not_found"
            else e.kind = invalid then "handled invalid"
            else "unknown".
    end
    verdict.
end

def main() =>
    (
        a: classify(kind: "missing"),
        b: classify(kind: "bad"),
        c: classify(kind: "fine")
    ).
end
