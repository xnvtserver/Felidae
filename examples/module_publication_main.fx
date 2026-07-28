# `main` executes only after the complete module has registered. It may call
# a method declared later in the same source file without a speculative retry.

main() =>
    return LaterDeclaration()

LaterDeclaration() =>
    return "complete module published"
