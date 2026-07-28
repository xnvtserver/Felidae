# Exact rules and uncertain evidence are intentionally separate.
import ("db", "fact.fx")

Person(name: "Asha", status: "active", risk: 2)
Person(name: "Bala", status: "active", risk: 8)
Applicant extend Person(name: "Asha", status: "active", risk: 2, program: "support")
Applicant extend Person(name: "Bala", status: "active", risk: 8, program: "support")

EligibilityProof(applicant: any) =>
    applicant.status == "active"
    applicant.risk < 5
    return {
        __type: "DerivationResult",
        exact: true,
        proof_status: "proved",
        conclusion: "eligible",
        rule: "active_low_risk_support",
        source_fact: applicant.name
    }
else
    return {
        __type: "DerivationResult",
        exact: false,
        proof_status: "not_proved",
        conclusion: "not_eligible",
        rule: "active_low_risk_support",
        source_fact: applicant.name
    }

main() =>
    asha := db.first(type: "Applicant", field: "name", equals: "Asha")
    bala := db.first(type: "Applicant", field: "name", equals: "Bala")
    exactAsha := EligibilityProof(applicant: asha)
    exactBala := EligibilityProof(applicant: bala)
    uncertain := fact.aggregateEvidence(evidence: [
        {source: "recent_income_signal", probability: 0.9, weight: 2},
        {source: "missing_document_signal", probability: 0.2, weight: 1}
    ])
    return {exact_asha: exactAsha, exact_bala: exactBala, evidence_only: uncertain}
