# Fact inheritance is also casing-neutral.
def Parent(a: 1).
def child extend Parent(a: 2).

def main() =>
    return child.count().
end
