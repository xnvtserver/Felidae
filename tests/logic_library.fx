import "logic".

def main() =>
    def rule := logicalImplication(antecedent: "rain", consequent: "wet").
    def converse := logicalConverse(rule: rule).
    def contrapositive := logicalContrapositive(rule: rule).
    def contradiction := logicalContradiction(positive: "wet", negative: "dry").
    (
        antecedent: converse.antecedent,
        consequent: converse.consequent,
        negated_antecedent: contrapositive.antecedent.input,
        negated_consequent: contrapositive.consequent.input,
        positive: contradiction.positive,
        negative: contradiction.negative
    ).
end
