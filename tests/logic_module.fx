# Regression coverage for core/logic.fx's dotted `Logic.*` declarations
# (def Logic.negate, Logic.implication, Logic.converse, Logic.contrapositive,
# Logic.contradiction). These were entirely unreachable: consumeQualifiedName
# refused to continue a dotted name past a capitalized first segment (so the
# `def Logic.negate(...)` declaration itself failed to parse), and separately
# a call to `Logic.negate(...)` in expression position fell through to the
# dynamic Object:invokeMember dispatch, which only ever succeeds when the
# receiver evaluates to a runtime fact/class value - "Logic" never does, it
# is a plain namespace prefix. Exercises both the declaration parsing fix
# and the call-site fallback fix, including a nested call
# (Logic.contrapositive calls Logic.negate twice from expression position).
import "logic"

def main() =>
    return (
        negated: Logic.negate(input: "sunny"),
        implied: Logic.implication(antecedent: "rain", consequent: "wet"),
        contrapositive: Logic.contrapositive(
            rule: Logic.implication(antecedent: "rain", consequent: "wet")),
        contradiction: Logic.contradiction(positive: "wet", negative: "dry")
    )
