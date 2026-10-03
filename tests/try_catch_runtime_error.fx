# A native runtime error inside try is caught: the catch variable holds
# {kind, message}, with kind "runtime".
def main() =>
    try
        rows := [{v: "b"}, {v: 1}].
        sorted := rows.order_by(field: "v").
    catch e then
        caught_kind := e.kind.
        caught_message := e.message.
    end
    return (kind: caught_kind, message: caught_message).
end
