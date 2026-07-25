import "fact.fx"

main() =>
    return fact.check_similarity(
        fact1: {name: "Rex"},
        fact2: {name: "Milo"},
        algorithm: "semantic_recursive",
        lexical_algorithm: "wu_palmer",
        field_alignment: "semantic",
        collection_mode: "ordered",
        missing_field_policy: "penalize",
        threshold: 2,
        maximum_depth: 16,
        maximum_fields: 128,
        explain: true
    )
