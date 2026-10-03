# order_by supports only numeric values; a string field is rejected, not sorted.
def main() =>
    rows := [{v: 3}, {v: "b"}, {v: 1}].
    return rows.order_by(field: "v").
end
