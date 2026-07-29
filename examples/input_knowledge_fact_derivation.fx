# Stored knowledge facts are queried like ordinary in-memory facts. A method
# combines them with input facts and returns a new, explicit decision fact.

EligibilityPolicy(name: "standard", minimum_score: 70, minimum_evidence: 2)
Application(applicant: Applicant(name: "template"), score: 0, evidence_count: 0, evidence: [])

DeriveEligibility(application: Application, policy: EligibilityPolicy) =>
    if application.score >= policy.minimum_score then
        return EligibilityDecision(
            applicant: application.applicant,
            status: "approved",
            score: application.score,
            policy: policy.name,
            evidence_count: application.evidence_count,
            rationale: "score satisfies stored policy"
        )
    else
        return EligibilityDecision(
            applicant: application.applicant,
            status: "review",
            score: application.score,
            policy: policy.name,
            evidence_count: application.evidence_count,
            rationale: "score below stored policy"
        )

main() =>
    inputApplicant := Applicant(name: "Ravi", region: "south")
    inputApplication := Application(
        applicant: inputApplicant,
        score: 82,
        evidence_count: 3,
        evidence: [IncomeEvidence(source: "payroll"), IdentityEvidence(source: "citizen-register")]
    )
    policy := Fact.first(type: "EligibilityPolicy", field: "name", equals: "standard")
    decision := DeriveEligibility(application: inputApplication, policy: policy)
    return decision
