# A self-contained, in-memory expert-system review.  Facts and rules are
# available directly to Felidae; db.fx is intentionally not imported because
# no file/database operation is involved.

Person(name: "person")
Applicant extend Person(id: "ava", name: "Ava", creditScore: 782, monthlyIncome: 6200, employment: "stable")
Applicant(id: "mira", name: "Mira", creditScore: 801, monthlyIncome: 6800, employment: "stable")
Applicant(id: "arun", name: "Arun", creditScore: 580, monthlyIncome: 3100, employment: "contract")

IncomeVerification(applicantId: "ava", verified: true)
CreditVerification(applicantId: "ava", verified: true)

LoanPolicy(id: "standard-policy", name: "standard")
PreferredLoanPolicy extend LoanPolicy(id: "preferred-policy", name: "preferred")

# Source-owned membership exposes only evidence relevant to the policy.  The
# policy family owns the final recommendation and can use attached relations.
Applicant.membership(input: Applicant, against: LoanPolicy) =>
    return ApplicantEvidence(
        applicantId: input.id,
        creditScore: input.creditScore,
        monthlyIncome: input.monthlyIncome,
        employment: input.employment
    )

LoanPolicy.compareMembership(context: Fact) =>
    verifiedIncome := Relation.find(input: context.relationships, name: "verified-income")
    if context.membership.creditScore >= 760 then
        if verifiedIncome != nil then
            return LoanDecisionAssessment(
                state: "approved",
                rule: "high_credit_with_verified_income",
                applicantId: context.membership.applicantId,
                evidence: context.membership,
                relationship: verifiedIncome
            )
        else
            return LoanDecisionAssessment(
                state: "manual-review",
                rule: "high_credit_without_income_relationship",
                applicantId: context.membership.applicantId,
                evidence: context.membership
            )
    else
        if context.membership.creditScore >= 650 then
            return LoanDecisionAssessment(
                state: "manual-review",
                rule: "medium_credit",
                applicantId: context.membership.applicantId,
                evidence: context.membership
            )
        else
            return LoanDecisionAssessment(
                state: "declined",
                rule: "low_credit",
                applicantId: context.membership.applicantId,
                evidence: context.membership
            )

# Presentation is intentionally separate from reasoning. The comparison still
# retains complete evidence and provenance, while the example prints a report
# a person can read without parsing one large nested value.
PrintDecision(decision: any) =>
    applicantLine := str.concat(left: "Applicant: ", right: decision.source.name)
    stateLine := str.concat(left: "Decision: ", right: decision.state)
    system.print(value: applicantLine)
    system.print(value: stateLine)
    if decision.state == "unresolved" then
        reasonLine := str.concat(left: "Reason: ", right: decision.reason)
        system.print(value: reasonLine)
        return decision.state
    else
        ruleLine := str.concat(left: "Rule: ", right: decision.rule)
        system.print(value: ruleLine)
        return decision.state

main() =>
    ava := Applicant(id: "ava", name: "Ava", creditScore: 782, monthlyIncome: 6200, employment: "stable")
    mira := Applicant(id: "mira", name: "Mira", creditScore: 801, monthlyIncome: 6800, employment: "stable")
    arun := Applicant(id: "arun", name: "Arun", creditScore: 580, monthlyIncome: 3100, employment: "contract")
    policy := PreferredLoanPolicy(id: "preferred-policy", name: "preferred")

    # Dependencies protect a decision from incomplete evidence.  Mira is
    # intentionally missing a verification fact and must remain unresolved.
    ava.depends(on: IncomeVerification(applicantId: "ava", verified: true))
    ava.depends(on: CreditVerification(applicantId: "ava", verified: true))
    mira.depends(on: IncomeVerification(applicantId: "mira", verified: true))
    ava.relate(to: policy, as: Relationship(name: "verified-income"), degree: 1.0, confidence: 0.98)

    approved := Relation.compare(left: ava, right: policy)
    incomplete := Relation.compare(left: mira, right: policy)
    declined := Relation.compare(left: arun, right: policy)
    system.print(value: "=== Loan policy decision report ===")
    PrintDecision(decision: approved)
    PrintDecision(decision: incomplete)
    PrintDecision(decision: declined)
    return "decision report complete"
