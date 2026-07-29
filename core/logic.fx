# Explicit logical transformations for expert-system rules.
#
# These functions construct auditable logical statements. They never claim
# that a converse or a contrapositive is proven merely because a source rule
# exists; user rules must evaluate the returned antecedent/consequent facts.

Logic.negate(input: any) =>
    return Negation(input: input)

Logic.implication(antecedent: any, consequent: any) =>
    return Implication(antecedent: antecedent, consequent: consequent)

Logic.converse(rule: any) =>
    return Implication(
        antecedent: rule.consequent,
        consequent: rule.antecedent,
        transformation: "converse"
    )

Logic.contrapositive(rule: any) =>
    return Implication(
        antecedent: Logic.negate(input: rule.consequent),
        consequent: Logic.negate(input: rule.antecedent),
        transformation: "contrapositive"
    )

Logic.contradiction(positive: any, negative: any) =>
    return ContradictionEvidence(
        positive: positive,
        negative: negative
    )
