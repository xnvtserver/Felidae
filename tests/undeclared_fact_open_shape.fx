# A fact is a class constructor. With no declared class it is not validated:
# the constructor declares a class internally and any field set is accepted,
# keyed by the first field of the type's first constructor.
Ghost(a: 1, b: "x").
Ghost(a: 2).
Ghost(a: 3, c: [1, 2], b: 0).

def main() =>
    return (count: Ghost.count(), only_a: Ghost.where(a: 2).count()).
end
