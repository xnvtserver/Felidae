# Relationships are ordinary facts (see deep_fact_reasoning_analysis.fx) --
# constructing Relationship(from:, to:, ...) retains it immediately, no
# relate() verb needed. Filtering "causal" edges out from "decorative" ones,
# and checking whether source and target share a causal signal, is plain
# lambda-based field filtering -- the same SQL-like WHERE style every other
# fact type already uses, not a bespoke comparison engine.

# Declaring one placeholder instance registers "Relationship" as a known
# fact type -- required for lambda(Relationship, ...) to be recognized as
# fact-query sugar rather than an unrecognized bare lambda call.
Relationship(from: nil, to: nil, name: "")

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

    sourceSignalEdge := Relationship(from: source, to: signal, name: "causal", scope: "eligibility")
    targetSignalEdge := Relationship(from: target, to: signal, name: "causal", scope: "eligibility")
    sourceDecorativeEdge1 := Relationship(from: source, to: decoration, name: "decorative", slot: 1)
    sourceDecorativeEdge2 := Relationship(from: source, to: decoration, name: "decorative", slot: 2)
    sourceDecorativeEdge3 := Relationship(from: source, to: decoration, name: "decorative", slot: 3)
    targetDecorativeEdge1 := Relationship(from: target, to: decoration, name: "decorative", slot: 4)
    targetDecorativeEdge2 := Relationship(from: target, to: decoration, name: "decorative", slot: 5)
    targetDecorativeEdge3 := Relationship(from: target, to: decoration, name: "decorative", slot: 6)

    # The decorative edges outnumber the causal ones 6 to 2, but a plain
    # field-equality filter -- no traversal engine -- keeps only what
    # matters: is source causally linked to the same signal as target?
    sourceCausalToSignal := lambda(Relationship,
        r => r.from == source and r.to == signal and r.name == "causal")
    targetCausalToSignal := lambda(Relationship,
        r => r.from == target and r.to == signal and r.name == "causal")
    connectedViaSignal := count(data: sourceCausalToSignal) > 0 and
                          count(data: targetCausalToSignal) > 0

    return (
        source_causal_to_signal: sourceCausalToSignal,
        target_causal_to_signal: targetCausalToSignal,
        connected_via_signal: connectedViaSignal
    )
