# A row that lacks the order_by field is rejected, not placed first or last.
def main() =>
    rows := [{v: 3}, {w: 0}, {v: 1}].
    return rows.order_by(field: "v").
end
