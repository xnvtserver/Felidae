import ("fact.fx", "fact_analysis.fx")

CandidateDietNeed(category: string, food: string, habitat: string, legs: number, wool: number, horns: number) =>
    return ({__type: "CandidateLivestock", __parent: "Ruminant", category: category, food: food, habitat: habitat, legs: legs, wool: wool, horns: horns})

ClassifyAnimal(candidate: any, corpus: array) =>
    goat := {__type: "Goat", __parent: "Ruminant", category: "livestock", food: "grass", habitat: "farm", legs: 4, wool: 0, horns: 2, weight: 54},
    sheep := {__type: "BlackSheep", __parent: "Ruminant", category: "livestock", food: "grass", habitat: "farm", legs: 4, wool: 8, horns: 0, weight: 65},
    wolf := {__type: "Wolf", __parent: "Canine", category: "wild", food: "meat", habitat: "forest", legs: 4, wool: 0, horns: 0, weight: 47},
    goatScore := fact.wuPalmerSimilarity(fact1: candidate, fact2: goat, facts: corpus),
    sheepScore := fact.wuPalmerSimilarity(fact1: candidate, fact2: sheep, facts: corpus),
    wolfScore := fact.wuPalmerSimilarity(fact1: candidate, fact2: wolf, facts: corpus),
    nearest := fact_analysis.nearestFactsWhere(input: candidate, candidates: [goat, sheep, wolf], count: 2, requiredFields: ["category", "food", "habitat"]),
    return (
        goatGraphScore: goatScore,
        sheepGraphScore: sheepScore,
        wolfGraphScore: wolfScore,
        nearestByProperties: nearest
    )

main() =>
    animal := {__type: "Animal", __parent: "LivingThing"},
    mammal := {__type: "Mammal", __parent: "Animal"},
    ruminant := {__type: "Ruminant", __parent: "Mammal"},
    canine := {__type: "Canine", __parent: "Mammal"},
    goat := {__type: "Goat", __parent: "Ruminant", category: "livestock", food: "grass", habitat: "farm", legs: 4, wool: 0, horns: 2, weight: 54},
    sheep := {__type: "BlackSheep", __parent: "Ruminant", category: "livestock", food: "grass", habitat: "farm", legs: 4, wool: 8, horns: 0, weight: 65},
    wolf := {__type: "Wolf", __parent: "Canine", category: "wild", food: "meat", habitat: "forest", legs: 4, wool: 0, horns: 0, weight: 47},
    tomato := {__type: "Tomato", __parent: "Fruit", category: "produce", food: "compost", habitat: "garden", legs: 0, wool: 0, horns: 0, weight: 1},
    corpus := [animal, mammal, ruminant, canine, goat, sheep, wolf, tomato],
    newAnimal := CandidateDietNeed(category: "livestock", food: "grass", habitat: "farm", legs: 4, wool: 2, horns: 1),

    direct := fact.directRelation(parent: ruminant, child: goat),
    ancestors := fact.ancestorClosure(fact: goat, facts: corpus),
    descendants := fact.descendantClosure(fact: mammal, facts: corpus),
    lca := fact.commonAncestor(fact1: goat, fact2: sheep, facts: corpus),
    path := fact.shortestPath(fact1: goat, fact2: wolf, facts: corpus),
    pathScore := fact.pathSimilarity(fact1: goat, fact2: wolf, facts: corpus),
    wupScore := fact.wuPalmerSimilarity(fact1: goat, fact2: sheep, facts: corpus),
    resnik := fact.resnikSimilarity(fact1: goat, fact2: sheep, facts: corpus),
    lin := fact.linSimilarity(fact1: goat, fact2: sheep, facts: corpus),
    stats := fact.frequencyStatistics(facts: corpus),
    normalized := fact.normalize(text: "Black Sheep"),
    clusters := fact_analysis.clusterFacts(facts: [goat, sheep, wolf, tomato], features: ["legs", "wool", "horns", "weight"], clusters: 2),
    classification := ClassifyAnimal(candidate: newAnimal, corpus: corpus),

    return (
        direct: direct,
        ancestors: ancestors,
        descendants: descendants,
        lowestCommonAncestor: lca,
        shortestPath: path,
        pathSimilarity: pathScore,
        wuPalmer: wupScore,
        resnik: resnik,
        lin: lin,
        frequencyStats: stats,
        normalized: normalized,
        clusters: clusters,
        classification: classification
    )
