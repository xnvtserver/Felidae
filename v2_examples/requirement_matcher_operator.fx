Element(name: "left", x: 4, y: 2)
Element(name: "right", x: 4, y: 7)

SimilarityRequirement extend OperatorRequirement(
    left: Element,
    right: Element
)

@matcher(
    operator: similarity,
    pattern: "{left} similarTo {right}",
    captures: {left: Element, right: Element},
    produces: [similarityRequirement: SimilarityRequirement],
    visibility: private
)
matchElementSimilarity() =>
    return RequirementMatch(
        similarityRequirement: SimilarityRequirement(
            left: left,
            right: right
        )
    )

@overload(
    operator: similarity,
    pattern: "{left} similarTo {right}",
    captures: {left: Element, right: Element},
    factor: similarityRequirement: SimilarityRequirement,
    result: SimilarityResult,
    cardinality: one,
    effects: pure,
    visibility: private
)
compareElementSimilarity() =>
    return SimilarityResult(
        left: similarityRequirement.left,
        right: similarityRequirement.right,
        sameX: similarityRequirement.left.x == similarityRequirement.right.x
    )

main() =>
    left := Element(name: "left", x: 4, y: 2)
    right := Element(name: "right", x: 4, y: 7)
    return left similarTo right
