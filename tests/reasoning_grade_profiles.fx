# Reasoning.grade's ReasoningProfile conjunction/evidence_aggregation
# policies now genuinely drive the degree/reliability combination instead of
# being validated then ignored (the code always computed with the product/
# maximum t-norm/t-conorm pair regardless of what a profile claimed). The
# default keeps producing the same numbers as before (0.8 * 0.5 = 0.4);
# naming "minimum" or "lukasiewicz" explicitly - the same three pairs
# core/fuzzy.fx exposes as fuzzyAnd/fuzzyAlgebraicAnd/fuzzyBoundedAnd - is
# the new, previously-decorative capability.
def main() =>
    default_result := Reasoning.grade(evidence: [Evidence(degree: 0.8, reliability: 0.5)])
    minimum_profile := ReasoningProfile(
        name: "minimum", conjunction: "minimum", disjunction: "maximum",
        evidence_aggregation: "maximum", negation: "one_minus")
    minimum_result := Reasoning.grade(
        evidence: [Evidence(degree: 0.8, reliability: 0.5)], profile: minimum_profile)
    return (
        default_support: default_result.support_degree,
        minimum_support: minimum_result.support_degree
    )
