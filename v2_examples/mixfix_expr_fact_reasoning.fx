# Mixfix captures can carry syntax trees and user-defined facts together.

Evidence(kind: "observed", source: "field-study")
Context(domain: "animal-behaviour", purpose: "classification")
Animal(name: "tiger", legs: 4, nocturnal: 1.0)
Animal(name: "cat", legs: 4, nocturnal: 0.0)

def explain(subject: expr, evidence: Evidence, context: Context, condition: expr) =>
    return Explanation(
        subject: subject,
        evidence: evidence,
        context: context,
        condition: condition
    )

@mixfix(
    pattern: "reason {subject: expr} using {evidence: Evidence} within {context: Context} when {condition: expr}"
)
def reasonFactValue() =>
    return explain(
        subject: subject,
        evidence: evidence,
        context: context,
        condition: condition
    )

@mixfix(
    pattern: "review {subject: expr} against {evidence: Evidence} within {context: Context} when {condition: expr}"
)
def reviewFactValue() =>
    return reason subject using evidence within context when condition

def main() =>
    tiger := Animal(name: "tiger")
    evidence := Evidence(kind: "observed")
    context := Context(domain: "animal-behaviour")
    result := review tiger against evidence within context when (tiger.legs == 4 and 1.0)
    return result
