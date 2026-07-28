# No Comparison.membership declaration is needed here. The built-in core
# normal method is loaded lazily and participates in ordinary dispatch.

Source(category: "base")
FirstTarget(category: "base")
ChainTarget(category: "different")

ChainTarget.compareMembership(context: Fact) =>
    return {
        state: "default-chain-interpreted",
        membership: context.membership,
        evidence: context.structuralEvidence
    }

main() =>
    source := Source(category: "base")
    firstTarget := FirstTarget(category: "base")
    chainTarget := ChainTarget(category: "different")
    first := Relation.compare(left: source, right: firstTarget)
    second := Relation.compare(left: first, right: chainTarget)
    return {first: first, second: second}
