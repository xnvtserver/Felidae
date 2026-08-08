# The first mixfix produces an expression-valued fact.  The second mixfix
# captures that complete mixfix expression as an expr and then invokes it.

Evidence(kind: "observed")
Context(domain: "animal-behaviour")

@mixfix(
    pattern: "reason {subject: expr} using {evidence: Evidence} within {context: Context}"
)
reasonFactValue() =>
    return Explanation(
        subject: subject,
        evidence: evidence,
        context: context
    )

@mixfix(
    pattern: "validate {claim: expr} with {expected: string}"
)
validateReasonValue() =>
    return Validation(
        claim: claim,
        expected: expected
    )

main() =>
    evidence := Evidence(kind: "observed")
    context := Context(domain: "animal-behaviour")
    claim := (reason "tiger" using evidence within context)
    result := validate claim with "explanation"
    direct := validate (reason "cat" using evidence within context) with "explanation"
    return (bound: result, direct: direct)
