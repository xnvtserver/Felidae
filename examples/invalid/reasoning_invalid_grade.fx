main() =>
    return Reasoning.grade(
        evidence: [
            Evidence(
                source: "invalid-sensor",
                degree: 1.25,
                reliability: 1.0,
                polarity: "support"
            )
        ]
    )
