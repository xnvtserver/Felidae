# Explicit logical transformations for expert-system rules.
#
# These functions construct auditable logical statements. They never claim
# that a converse or a contrapositive is proven merely because a source rule
# exists; user rules must evaluate the returned antecedent/consequent facts.

def Logic.negate(input: any) =>
    return Negation(input: input)

def Logic.implication(antecedent: any, consequent: any) =>
    return Implication(antecedent: antecedent, consequent: consequent)

def Logic.converse(rule: any) =>
    return Implication(
        antecedent: rule.consequent,
        consequent: rule.antecedent,
        transformation: "converse"
    )

def Logic.contrapositive(rule: any) =>
    return Implication(
        antecedent: Logic.negate(input: rule.consequent),
        consequent: Logic.negate(input: rule.antecedent),
        transformation: "contrapositive"
    )

def Logic.contradiction(positive: any, negative: any) =>
    return ContradictionEvidence(
        positive: positive,
        negative: negative
    )
