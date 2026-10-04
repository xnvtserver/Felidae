# order_by sorts numerically and is stable for equal keys.
def main() =>
    def rows := [{id: "a", v: 3}, {id: "b", v: 1}, {id: "c", v: 2}, {id: "d", v: 1}].
    return rows.order_by(field: "v").
end
