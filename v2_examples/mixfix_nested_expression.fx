# The first mixfix produces an expression-valued fact.  The second mixfix
# captures that complete mixfix expression as an expr and then invokes it.

def Evidence(kind: "observed").
def Context(domain: "animal-behaviour").

@mixfix(
    pattern: "reason {subject: expr} using {evidence: Evidence} within {context: Context}"
)
def reasonFactValue() =>
    return Explanation(
        subject: subject,
        evidence: evidence,
        context: context
    ).
end

@mixfix(
    pattern: "validate {claim: expr} with {expected: string}"
)
def validateReasonValue() =>
    return Validation(
        claim: claim,
        expected: expected
    ).
end

def main() =>
    def evidence := Evidence(kind: "observed").
    def context := Context(domain: "animal-behaviour").
    def claim := (reason "tiger" using evidence within context).
    def result := validate claim with "explanation".
    def direct := validate (reason "cat" using evidence within context) with "explanation".
    return (bound: result, direct: direct).
end
