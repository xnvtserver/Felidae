# A repeated named argument - in a `def` head's own parameter list, or at a
# call site - must be rejected at parse time. This used to be silently
# accepted (the last occurrence quietly won), unlike a class body's already-
# rejected repeated field or a mixfix pattern's already-rejected repeated
# capture; parseArguments is the one parser shared by all three shapes, so
# fixing it there closes the gap for all of them at once.
def foo(x: number, x: number) =>
    return x

def main() =>
    return foo(x: 1, x: 2)
