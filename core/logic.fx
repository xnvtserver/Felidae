# Explicit logical transformations for expert-system rules.
#
# These functions construct auditable logical statements. They never claim
# that a converse or a contrapositive is proven merely because a source rule
# exists; user rules must evaluate the returned antecedent/consequent facts.

def logicalNegate(input: any) =>
    return Negation(input: input)
end

def logicalImplication(antecedent: any, consequent: any) =>
    return Implication(antecedent: antecedent, consequent: consequent)
end

def logicalConverse(rule: any) =>
    return Implication(
        antecedent: rule.consequent,
        consequent: rule.antecedent,
        transformation: "converse"
    )
end

def logicalContrapositive(rule: any) =>
    return Implication(
        antecedent: logicalNegate(input: rule.consequent),
        consequent: logicalNegate(input: rule.antecedent),
        transformation: "contrapositive"
    )
end

def logicalContradiction(positive: any, negative: any) =>
    return ContradictionEvidence(
        positive: positive,
        negative: negative
    )
end
