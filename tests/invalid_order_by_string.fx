# order_by supports only numeric values; a string field is rejected, not sorted.
def main() =>
    def rows := [{v: 3}, {v: "b"}, {v: 1}].
    rows.order_by(field: "v").
end
