class Leaf
    def value: number.
end

class Branch
    def leaf: obj.
end

class Root
    def branch: obj.
end

def main() =>
    def a := Root(branch: Branch(leaf: Leaf(value: 42))).
    def k := a.branch.leaf.value.
    return k.
end
