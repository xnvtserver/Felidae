# An expression result is determined by its Felidae method. It can be a
# primitive proof, a probability fact, a recoverable error fact, another fact,
# or a list of facts.

Animal(name: "", legs: 0)
Cat extend Animal(name: "sony", legs: 4)

Probability(fact: nil, chance: 0)
Error(reason: "", code: "")
Classification(fact: nil, category: "")
Evidence(subject: nil, field: "", value: nil)

@mixfix(
    pattern: "evaluate {subject: Animal} as {mode: string}"
)
evaluateFactExpression() =>
    if mode == "crisp" then
        return subject.legs == 4
    else
        if mode == "probability" then
            return Probability(fact: subject, chance: 0.20)
        else
            if mode == "error" then
                return Error(reason: "unsupported classification", code: "UnsupportedMode")
            else
                if mode == "derived" then
                    return Classification(fact: subject, category: "companion")
                else
                    return [
                        Classification(fact: subject, category: "companion"),
                        Evidence(subject: subject, field: "legs", value: subject.legs)
                    ]

main() =>
    cat := Cat(name: "sony", legs: 4)
    crisp := evaluate cat as "crisp"
    probability := evaluate cat as "probability"
    error := evaluate cat as "error"
    derived := evaluate cat as "derived"
    evidence := evaluate cat as "evidence"
    return (
        crisp: crisp,
        probability: probability,
        error: error,
        derived: derived,
        evidence: evidence
    )
