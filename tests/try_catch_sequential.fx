# Two try blocks in one method may reuse the catch variable name: it is scoped
# to its own catch body.
def main() =>
    try
        throw(exception: {kind: "first", message: "one"}).
    catch e then
        first := e.message.
    end
    try
        throw(exception: {kind: "second", message: "two"}).
    catch e then
        second := e.message.
    end
    return (first: first, second: second).
end
