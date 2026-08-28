# `where` is a sequential guard, not a fact-query construct here. Statements
# retain their order: those before a guard run at the current level, while
# those after it run only if the guard succeeds. If any `where` fails,
# execution falls through to the clause's single `else` branch. The compiler
# normalizes this to ordinary nested if/else AST nodes; no extra opcode or
# runtime path is involved.
#
# A `where` guard with no `else` at all is intentionally a compile error
# ("a where-guarded clause requires exactly one else branch") rather than a
# guessed runtime failure behavior.

eligible(score: number, active: number) =>
    adjusted := score + 0
    where score >= 70
    where active == 1.0
    return adjusted
else
    return (0.0)

main() =>
    return (
        passes: eligible(score: 82, active: 1.0),
        lowScore: eligible(score: 40, active: 1.0),
        inactive: eligible(score: 90, active: 0.0)
    )
