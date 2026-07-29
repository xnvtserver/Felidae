# Demand-driven recursive proof with explicit contrary evidence.

Route(from: "A", to: "B")
Route(from: "B", to: "C")
Route(from: "C", to: "D")
BlockedRoute(from: "A", to: "D")

Reachable(from: from, to: to) =>
    Route(from: from, to: to)
    return

Reachable(from: from, to: to) =>
    Route(from: from, to: next)
    Reachable(from: next, to: to)
    return

NotReachable(from: from, to: to) =>
    BlockedRoute(from: from, to: to)
    return

main() =>
    Reasoning.contrary(
        positive: "Reachable",
        negative: "NotReachable"
    )
    exact := Reasoning.prove(
        query: Reachable(from: "A", to: "D")
    )
    advisory := Reasoning.grade(
        conclusion: Reachable(from: "A", to: "D"),
        evidence: [
            Evidence(
                source: "route-telemetry",
                degree: 0.92,
                reliability: 0.80,
                polarity: "support"
            ),
            Evidence(
                source: "closure-notice",
                degree: 0.75,
                reliability: 0.90,
                polarity: "oppose"
            )
        ]
    )
    return ReasoningAudit(exact: exact, advisory: advisory)
