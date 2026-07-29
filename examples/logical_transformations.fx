import "logic"

# A rule and its converse are distinct claims. The example keeps both as
# explicit facts, so an expert system can audit which direction was actually
# proved instead of inferring a converse silently.

main() =>
    adult := Adult(person: "Ravi")
    eligible := EligibleVoter(person: "Ravi")
    rule := Logic.implication(antecedent: adult, consequent: eligible)
    converse := Logic.converse(rule: rule)
    contrapositive := Logic.contrapositive(rule: rule)
    conflicting := Logic.contradiction(
        positive: eligible,
        negative: Logic.negate(input: eligible)
    )
    return LogicalTransformationReport(
        rule: rule,
        converse: converse,
        contrapositive: contrapositive,
        conflict: conflicting
    )
