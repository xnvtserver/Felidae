# `nil` is a value, not an unbound-variable marker. Every local can be
# assigned exactly once regardless of the assigned value.
def main() =>
    def result := nil.
    def result := 42.
    return result.
end
