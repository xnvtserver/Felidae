# Link edges are durable graph data. Their immutable properties distinguish
# causal evidence from decorative metadata without a relationship class.

Entity(name: "entity", active: 1.0)
Source extend Entity(name: "source", active: 1.0)
Target extend Entity(name: "target", active: 1.0)
Signal extend Entity(name: "signal", active: 1.0)
Decoration extend Entity(name: "decoration", active: 1.0)

def Source.membership(input: Source, against: Entity) =>
    return (active: input.active)
end

def Target.membership(input: Target, against: Entity) =>
    return (active: input.active)
end

def main() =>
    sources := lambda(Source, fact => fact.name == "source")
    targets := lambda(Target, fact => fact.name == "target")
    signals := lambda(Signal, fact => fact.name == "signal")
    decorations := lambda(Decoration, fact => fact.name == "decoration")

    source := array.get(data: sources, position: 0)
    target := array.get(data: targets, position: 0)
    signal := array.get(data: signals, position: 0)
    decoration := array.get(data: decorations, position: 0)

    sourceSignalEdge := Link(from: source, to: signal, properties: {kind: "causal", scope: "eligibility"})
    targetSignalEdge := Link(from: target, to: signal, properties: {kind: "causal", scope: "eligibility"})
    sourceDecorativeEdge1 := Link(from: source, to: decoration, properties: {kind: "decorative", slot: 1})
    sourceDecorativeEdge2 := Link(from: source, to: decoration, properties: {kind: "decorative", slot: 2})
    sourceDecorativeEdge3 := Link(from: source, to: decoration, properties: {kind: "decorative", slot: 3})
    targetDecorativeEdge1 := Link(from: target, to: decoration, properties: {kind: "decorative", slot: 4})
    targetDecorativeEdge2 := Link(from: target, to: decoration, properties: {kind: "decorative", slot: 5})
    targetDecorativeEdge3 := Link(from: target, to: decoration, properties: {kind: "decorative", slot: 6})

    # The decorative edges outnumber the causal ones 6 to 2. The property
    # condition selects causal Links; the trailing primary-key predicate
    # verifies that both nodes point at the same signal.
    sourceCausalToSignal := Source().where(name: "source")
        .join(properties: {kind: "causal"}, direction: forward.class)
        .where(right.name == "signal")
    targetCausalToSignal := Target().where(name: "target")
        .join(properties: {kind: "causal"}, direction: forward.class)
        .where(right.name == "signal")
    connectedViaSignal := count(data: sourceCausalToSignal) > 0 and
                          count(data: targetCausalToSignal) > 0

    return (
        source_causal_to_signal: sourceCausalToSignal,
        target_causal_to_signal: targetCausalToSignal,
        connected_via_signal: connectedViaSignal
    )
end
