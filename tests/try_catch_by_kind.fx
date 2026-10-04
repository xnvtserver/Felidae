# Different exceptions are told apart by kind inside one catch body.
def classify(kind: string) =>
    try
        if kind == "missing" then
            throw(exception: {kind: "not_found", message: "no such row"}).
        elif kind == "bad" then
            throw(exception: {kind: "invalid", message: "bad input"}).
        end
        def verdict := "ok".
    catch e then
        if e.kind == "not_found" then
            def verdict := "handled not_found".
        elif e.kind == "invalid" then
            def verdict := "handled invalid".
        else
            def verdict := "unknown".
        end
    end
    return verdict.
end

def main() =>
    return (
        a: classify(kind: "missing"),
        b: classify(kind: "bad"),
        c: classify(kind: "fine")
    ).
end
