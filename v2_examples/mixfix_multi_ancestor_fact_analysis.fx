# Fact comparisons are evidence-producing operations. The mixfix expression
# delegates to an ordinary method, which returns a report fact rather than a
# boolean. Every compared value is retrieved from the fact store.

Living(name: "")

WarmBlooded extend Living(
    name: "",
    warm_blooded: true
)

FourLegged extend Living(
    name: "",
    legs: 4
)

Mammal extend WarmBlooded, FourLegged(
    name: "",
    diet: "mixed"
)

Feline extend Mammal(
    name: "",
    family: "feline"
)

Canine extend Mammal(
    name: "",
    family: "canine"
)

Tiger extend Feline(
    name: "shira",
    species: "tiger",
    habitat: "wild",
    diet: "carnivore"
)

Cat extend Feline(
    name: "sony",
    species: "cat",
    habitat: "home",
    diet: "carnivore"
)

Dog extend Canine(
    name: "max",
    species: "dog",
    habitat: "home",
    diet: "omnivore"
)

SimilarityReport(
    context: "",
    left_type: "",
    right_type: "",
    common_ancestor: nil,
    score: 0,
    evidence: []
)

Mammal.membership(input: Mammal, against: Mammal) =>
    return {
        warm_blooded: input.warm_blooded,
        legs: input.legs,
        diet: input.diet,
        family: input.family,
        species: input.species,
        habitat: input.habitat
    }

buildSimilarityReport(left: Mammal, right: Mammal, context: string) =>
    comparison := Relation.compare(left: left, right: right, max_depth: 6)
    evidence := comparison.evidence
    return SimilarityReport(
        context: context,
        left_type: type(left),
        right_type: type(right),
        common_ancestor: evidence.matchedAncestor,
        score: evidence.similarity,
        evidence: [
            evidence,
            EvidenceSummary(
                ancestor_count: array.len(data: evidence.ancestorEvidence),
                matched_fields: evidence.matchedFields,
                conflicting_fields: evidence.conflictingFields
            )
        ]
    )

@mixfix(
    pattern: "compare {left: Mammal} through {context: string} with {right: Mammal}"
)
compareFactsWithContext() =>
    return buildSimilarityReport(left: left, right: right, context: context)

main() =>
    tigers := lambda(Tiger, fact => fact.name == "shira")
    cats := lambda(Cat, fact => fact.name == "sony")
    dogs := lambda(Dog, fact => fact.name == "max")

    tiger := array.get(data: tigers, position: 0)
    cat := array.get(data: cats, position: 0)
    dog := array.get(data: dogs, position: 0)

    feline_pair := compare tiger through "shared-feline-lineage" with cat
    mammal_pair := compare cat through "shared-mammal-lineage" with dog
    return [feline_pair, mammal_pair]
