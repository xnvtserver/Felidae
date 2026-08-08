# A policy-style expert system: mixfix expressions participate in normal
# method composition, if/then/else control flow, and pipeline evaluation.

Person(name: "")
CreditProfile extend Person(credit: 0)
IncomeProfile extend Person(income: 0)
Applicant extend CreditProfile, IncomeProfile(
    name: "",
    credit: 0,
    income: 0,
    verified: false
)

Policy(
    name: "",
    minimum_credit: 0,
    minimum_income: 0,
    minimum_score: 0
)

Assessment(
    applicant: nil,
    policy: nil,
    credit_score: 0,
    income_score: 0,
    verification_score: 0,
    score: 0,
    evidence: []
)

RuleEvidence(rule: "", satisfied: false, contribution: 0)
ApprovalDecision(applicant: nil, policy: nil, score: 0, evidence: [])
Error(reason: "", code: "", subject: nil)

creditScore(subject: Applicant, policy: Policy) =>
    if subject.credit >= policy.minimum_credit then
        return 0.45
    else
        return 0.10

incomeScore(subject: Applicant, policy: Policy) =>
    if subject.income >= policy.minimum_income then
        return 0.35
    else
        return 0.05

verificationScore(subject: Applicant) =>
    if subject.verified == true then
        return 0.20
    else
        return 0

@mixfix(
    pattern: "assess {subject: Applicant} against {policy: Policy}"
)
assessApplicant() =>
    credit := creditScore(subject: subject, policy: policy)
    income := incomeScore(subject: subject, policy: policy)
    verification := verificationScore(subject: subject)
    return Assessment(
        applicant: subject,
        policy: policy,
        credit_score: credit,
        income_score: income,
        verification_score: verification,
        score: credit + income + verification,
        evidence: [
            RuleEvidence(
                rule: "credit",
                satisfied: subject.credit >= policy.minimum_credit,
                contribution: credit
            ),
            RuleEvidence(
                rule: "income",
                satisfied: subject.income >= policy.minimum_income,
                contribution: income
            ),
            RuleEvidence(
                rule: "verification",
                satisfied: subject.verified == true,
                contribution: verification
            )
        ]
    )

@mixfix(
    pattern: "{assessment: Assessment} qualifies for {minimum: number}"
)
assessmentQualifies() =>
    return assessment.score >= minimum

@mixfix(
    pattern: "explain {assessment: Assessment}"
)
explainAssessment() =>
    return ApprovalDecision(
        applicant: assessment.applicant,
        policy: assessment.policy,
        score: assessment.score,
        evidence: assessment.evidence
    )

decide(subject: Applicant, policy: Policy) =>
    assessment := assess subject against policy
    if assessment qualifies for policy.minimum_score then
        return assessment then explain system.result
    else
        return Error(
            reason: "policy evidence did not reach the required score",
            code: "InsufficientEvidence",
            subject: subject
        )

Applicant(name: "ava", credit: 780, income: 120000, verified: true)
Applicant(name: "mira", credit: 580, income: 35000, verified: false)
Policy(name: "prime", minimum_credit: 700, minimum_income: 80000, minimum_score: 0.80)

main() =>
    approvedApplicants := lambda(Applicant, fact => fact.name == "ava")
    declinedApplicants := lambda(Applicant, fact => fact.name == "mira")
    policies := lambda(Policy, fact => fact.name == "prime")
    ava := array.get(data: approvedApplicants, position: 0)
    mira := array.get(data: declinedApplicants, position: 0)
    prime := array.get(data: policies, position: 0)
    return [
        decide(subject: ava, policy: prime),
        decide(subject: mira, policy: prime)
    ]
