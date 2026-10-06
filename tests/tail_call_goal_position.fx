# A goal-position call to a method that ends in `return other(x)` must not
# replace the enclosing value call: outer() keeps running and returns 100.
def other(x: number) =>
    x + 1.
end

def helper(x: number) =>
    other(x).
end

def outer(x: number) =>
    helper(x).
    100.
end

def main() =>
    outer(1).
end
