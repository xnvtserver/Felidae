# A native runtime error inside try is caught: the catch variable holds
# {kind, message}, with kind "runtime".
def main() =>
    try
        def rows := [{v: "b"}, {v: 1}].
        def sorted := rows.order_by(field: "v").
    catch e then
        def caught_kind := e.kind.
        def caught_message := e.message.
    end
    (kind: caught_kind, message: caught_message).
end
