class Leaf
    value: number.
end

class Branch
    leaf: obj.
end

class Root
    branch: obj.
end

def main() =>
    a := Root(branch: Branch(leaf: Leaf(value: 42))).
    k := a.branch.leaf.value.
    return k.
end
