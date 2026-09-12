Root(code: "root", enabled: 1.0)

class Layer extends Root
    depth: number
end

class Leaf extends Layer
    label: string
end

def main() =>
    leaf := Leaf(depth: 2, label: "mixed")
    return (
        inherited_fact_field: leaf.code,
        inherited_class_field: leaf.depth,
        own_field: leaf.label
    )
end
