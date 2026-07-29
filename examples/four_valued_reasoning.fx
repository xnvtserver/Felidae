# Exact Datalog truth and advisory grades are deliberately separate.

Eligible(name: "Ravi")
Ineligible(name: "Ravi")
Eligible(name: "Anu")
Ineligible(name: "Teo")

main() =>
    contrary := Reasoning.contrary(
        positive: "Eligible",
        negative: "Ineligible"
    )
    ravi := Reasoning.prove(query: Eligible(name: "Ravi"))
    anu := Reasoning.prove(query: Eligible(name: "Anu"))
    teo := Reasoning.prove(query: Eligible(name: "Teo"))
    mira := Reasoning.prove(query: Eligible(name: "Mira"))
    evidence := [
        Evidence(
            source: "policy-score",
            degree: 0.90,
            reliability: 0.80,
            polarity: "support"
        ),
        Evidence(
            source: "risk-review",
            degree: 0.60,
            reliability: 0.75,
            polarity: "oppose"
        )
    ]
    decision := Reasoning.decide(
        query: Eligible(name: "Ravi"),
        evidence: evidence,
        probability: 0.70,
        similarity: 0.85,
        profile: ReasoningProfile(
            name: "auditable-default",
            conjunction: "minimum",
            disjunction: "maximum",
            evidence_aggregation: "maximum",
            negation: "one_minus"
        )
    )
    return ReasoningReport(
        contrary: contrary,
        ravi: ravi,
        anu: anu,
        teo: teo,
        mira: mira,
        decision: decision
    )
