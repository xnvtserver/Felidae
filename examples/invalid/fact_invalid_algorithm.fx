import "fact.fx"

main() =>
    return fact.check_similarity(
        fact1: {name: "Rex"},
        fact2: {name: "Milo"},
        algorithm: "approximate_magic"
    )
