# Explicit `end` delimiters close methods and nested decisions while the AST
# interpreter preserves the existing `then` pipeline semantics.

def increment(value: number) =>
    return value + 1
end

def double(value: number) =>
    return value * 2
end

def evaluate(value: number) =>
    if value >= 0 then
        processed := increment(value: value)
            then double(value: system.result)
        return processed
    else
        return 0.0
    end
end

def main() =>
    return (
        accepted: evaluate(value: 4),
        rejected: evaluate(value: -4)
    )
end
