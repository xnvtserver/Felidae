import ("fact.fx").

main() =>
    result := fact.compareFacts(fact1: "goat", fact2: {__type: "Goat", __parent: "Ruminant"}),
    return (result: result).
