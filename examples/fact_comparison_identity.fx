# Stored facts keep a runtime-only identity even when their visible fields are
# structurally equal.  The identity is carried by Fact.all()/Fact.select()
# values, not printed or exported as part of the fact.

Twin(label: "same")
Twin(label: "same")
Marker(name: "attachment-target")
Marker(name: "comparison-target")

Marker.compareMembership(context: Fact) =>
    return {
        state: "identity-aware",
        relationshipCount: count(context.relationships),
        evidence: context.structuralEvidence
    }

main() =>
    twins := Fact.all(type: "Twin")
    first := array.get(data: twins, position: 0)
    second := array.get(data: twins, position: 1)
    attachmentTarget := Fact.first(type: "Marker", field: "name", equals: "attachment-target")
    comparisonTarget := Fact.first(type: "Marker", field: "name", equals: "comparison-target")
    alias := second
    alias.relate(
        to: attachmentTarget,
        as: Relationship(name: "attached-to-second"),
        degree: 0.5,
        confidence: 0.7
    )
    # Repeating the same immutable attachment is idempotent.
    alias.relate(
        to: attachmentTarget,
        as: Relationship(name: "attached-to-second"),
        degree: 0.5,
        confidence: 0.7
    )
    firstResult := Relation.compare(left: first, right: comparisonTarget)
    secondResult := Relation.compare(left: second, right: comparisonTarget)
    return {
        first_count: firstResult.relationshipCount,
        second_count: secondResult.relationshipCount,
        first: firstResult,
        second: secondResult
    }
