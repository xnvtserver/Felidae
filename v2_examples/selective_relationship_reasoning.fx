# Relation.compare can constrain graph expansion with a normal Relationship
# fact pattern. The decorative edges are stored knowledge, but they are not
# relevant to this causal comparison and must be pruned before traversal.

Entity(name: "entity", active: true)
Source extend Entity(name: "source", active: true)
Target extend Entity(name: "target", active: true)
Signal extend Entity(name: "signal", active: true)
Decoration extend Entity(name: "decoration", active: true)

Source.membership(input: Source, against: Entity) =>
    return (active: input.active)

Target.membership(input: Target, against: Entity) =>
    return (active: input.active)

main() =>
    sources := lambda(Source, fact => fact.name == "source")
    targets := lambda(Target, fact => fact.name == "target")
    signals := lambda(Signal, fact => fact.name == "signal")
    decorations := lambda(Decoration, fact => fact.name == "decoration")

    source := array.get(data: sources, position: 0)
    target := array.get(data: targets, position: 0)
    signal := array.get(data: signals, position: 0)
    decoration := array.get(data: decorations, position: 0)

    source.relate(to: signal, as: Relationship(name: "causal", scope: "eligibility"))
    target.relate(to: signal, as: Relationship(name: "causal", scope: "eligibility"))
    source.relate(to: decoration, as: Relationship(name: "decorative", slot: 1))
    source.relate(to: decoration, as: Relationship(name: "decorative", slot: 2))
    source.relate(to: decoration, as: Relationship(name: "decorative", slot: 3))
    target.relate(to: decoration, as: Relationship(name: "decorative", slot: 4))
    target.relate(to: decoration, as: Relationship(name: "decorative", slot: 5))
    target.relate(to: decoration, as: Relationship(name: "decorative", slot: 6))

    return Relation.compare(
        left: source,
        right: target,
        max_depth: 3,
        relationship: Relationship(name: "causal")
    )
