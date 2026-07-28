# Auditable expert-system result helpers.  These functions deliberately do
# not perform fuzzy matching or probability aggregation: exact proof status
# and uncertain evidence remain separate values.

reasoning.contrary(positive: string, negative: string) =>
    return {
        __type: "ContraryPredicate",
        positive: positive,
        negative: negative
    }

reasoning.exact(conclusion: string, proved: bool, rule: string, sources: array) =>
    if proved == true then
        return {
            __type: "DerivationResult",
            exact: true,
            proof_status: "proved",
            truth_status: "proved",
            conclusion: conclusion,
            rule: rule,
            source_fact_ids: sources,
            evidence_inputs: []
        }
    else
        return {
            __type: "DerivationResult",
            exact: false,
            proof_status: "not_proved",
            truth_status: "unknown",
            conclusion: conclusion,
            rule: rule,
            source_fact_ids: sources,
            evidence_inputs: []
        }

reasoning.assess(contrary: any, positive: any, negative: any) =>
    if positive.proof_status == "proved" then
        if negative.proof_status == "proved" then
            return {
                __type: "DerivationResult",
                exact: true,
                truth_status: "both",
                conclusion: contrary.positive,
                contrary_conclusion: contrary.negative,
                contradictory: true,
                proof: positive,
                contrary_proof: negative,
                evidence_inputs: []
            }
        else
            return {
                __type: "DerivationResult",
                exact: true,
                truth_status: "proved",
                conclusion: contrary.positive,
                contrary_conclusion: contrary.negative,
                contradictory: false,
                proof: positive,
                contrary_proof: negative,
                evidence_inputs: []
            }
    else
        if negative.proof_status == "proved" then
            return {
                __type: "DerivationResult",
                exact: true,
                truth_status: "disproved",
                conclusion: contrary.positive,
                contrary_conclusion: contrary.negative,
                contradictory: false,
                proof: positive,
                contrary_proof: negative,
                evidence_inputs: []
            }
        else
            return {
                __type: "DerivationResult",
                exact: false,
                truth_status: "unknown",
                conclusion: contrary.positive,
                contrary_conclusion: contrary.negative,
                contradictory: false,
                proof: positive,
                contrary_proof: negative,
                evidence_inputs: []
            }
