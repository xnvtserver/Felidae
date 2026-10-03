import "logic".

def main() =>
    rule := logicalImplication(antecedent: "rain", consequent: "wet").
    converse := logicalConverse(rule: rule).
    contrapositive := logicalContrapositive(rule: rule).
    contradiction := logicalContradiction(positive: "wet", negative: "dry").
    return (
        antecedent: converse.antecedent,
        consequent: converse.consequent,
        negated_antecedent: contrapositive.antecedent.input,
        negated_consequent: contrapositive.consequent.input,
        positive: contradiction.positive,
        negative: contradiction.negative
    ).
end
