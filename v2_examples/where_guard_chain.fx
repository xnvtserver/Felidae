# `where` is a sequential guard chain, not a fact-query construct here: each
# `where <condition>` before the terminal `return` must hold, in order, for
# that `return` to run. If any `where` fails, execution falls through to the
# clause's single `else` branch instead -- there is no silent nil result and
# no new opcode involved, the compiler desugars this into an ordinary nested
# if/else at compile time (see IrCodeGenerator.cpp's desugarWhereGuards).
#
# A `where` guard with no `else` at all is intentionally a compile error
# ("a where-guarded clause requires exactly one else branch") rather than a
# guessed runtime failure behavior.

eligible(score: number, active: bool) =>
    where score >= 70
    where active == true
        return true
    else
    return false

main() =>
    return (
        passes: eligible(score: 82, active: true),
        lowScore: eligible(score: 40, active: true),
        inactive: eligible(score: 90, active: false)
    )
