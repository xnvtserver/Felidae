# The key field fixed by the first constructor must be present in every fact
# of the type, even though no other field is validated.
Ghost(a: 1, b: "x").
Ghost(b: 2).

def main() =>
    return Ghost.count().
end
