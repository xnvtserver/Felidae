# Two try blocks in one method may reuse the catch variable name: it is scoped
# to its own catch body.
def main() =>
    try
        throw(kind: first, message: "one").
    catch e then
        def first := e.message.
    end
    try
        throw(kind: second, message: "two").
    catch e then
        def second := e.message.
    end
    (first: first, second: second).
end
