# A method parameter names a type; a literal is not a pattern, so this must be
# rejected instead of silently matching every call.
def sumTo(n: 0, acc: number) =>
    acc.
end

def main() =>
    sumTo(n: 5, acc: 3).
end
