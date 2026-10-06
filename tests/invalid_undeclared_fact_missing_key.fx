# The key field fixed by the first constructor must be present in every fact
# of the type, even though no other field is validated.
def Ghost(a: 1, b: "x").
def Ghost(b: 2).

def main() =>
    Ghost.count().
end
