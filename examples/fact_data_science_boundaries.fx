import ("json", "probability", "ml", "fact.fx", "fact_analysis.fx")

AnimalNeed(category: string, diet: string, habitat: string, wool: number, horns: number, milk: number, weight: number) =>
    return ({category: category, diet: diet, habitat: habitat, wool: wool, horns: horns, milk: milk, weight: weight})

RuminantSynset(left: any, right: any) =>
    common := fact.commonAncestor(fact1: left, fact2: right),
    similarity := fact.compareFacts(fact1: left, fact2: right),
    return (
        __type: "RuminantSynset",
        source: "generated_from_fact_comparison",
        ancestor: common.ancestor_type,
        generalized: common.generalized_fact,
        similarity: similarity.score,
        differences: similarity.differences
    )

main() =>
    goat := {__type: "Goat", __parent: "Ruminant", name: "BlackGoat", category: "livestock", diet: "grass", habitat: "farm", coatColor: "black", wool: 0.2, horns: 0.9, milk: 0.7, weight: 42.5},
    sheep := {__type: "Sheep", __parent: "Ruminant", name: "BlackSheep", category: "livestock", diet: "grass", habitat: "farm", coatColor: "black", wool: 0.95, horns: 0.4, milk: 0.5, weight: 48.2},
    wolf := {__type: "Wolf", __parent: "Carnivore", name: "GreyWolf", category: "wild", diet: "meat", habitat: "forest", coatColor: "grey", wool: 0.0, horns: 0.0, milk: 0.1, weight: 51.0},
    animalFacts := [goat, sheep, wolf],
    livestockNeed := AnimalNeed(category: "livestock", diet: "grass", habitat: "farm", wool: 0.8, horns: 0.5, milk: 0.6, weight: 45.0),
    goatMilk := json.get(data: goat, key: "milk"),
    goatHorns := json.get(data: goat, key: "horns"),
    goatWool := json.get(data: goat, key: "wool"),
    goatWeight := json.get(data: goat, key: "weight"),
    sheepWeight := json.get(data: sheep, key: "weight"),
    goatMilkScore := goatMilk * 0.45,
    goatHornScore := goatHorns * 0.25,
    goatLowWoolScore := 1 - goatWool,
    goatLowWoolWeighted := goatLowWoolScore * 0.30,
    goatTotalWeight := goatWeight + sheepWeight,

    floatArithmetic := {
        sum: 0.1 + 0.2,
        weightedScore: goatMilkScore + goatHornScore + goatLowWoolWeighted,
        normalizedWeight: goatWeight / goatTotalWeight
    },
    numericStats := {
        meanWeight: probability.mean(data: [goat.weight, sheep.weight, wolf.weight]),
        varianceWeight: probability.variance(data: [goat.weight, sheep.weight, wolf.weight]),
        stddevWeight: probability.stddev(data: [goat.weight, sheep.weight, wolf.weight]),
        milkWoolCorrelation: probability.correlation(left: [goat.milk, sheep.milk, wolf.milk], right: [goat.wool, sheep.wool, wolf.wool]),
        entropy: probability.entropy(data: probability.normalize(data: [goat.wool, sheep.wool, wolf.wool]))
    },
    profile := fact_analysis.profileFacts(facts: animalFacts, features: ["wool", "horns", "milk", "weight"]),
    clusters := fact_analysis.clusterFacts(facts: animalFacts, features: ["wool", "horns", "milk", "weight"], clusters: 2),
    classifier := fact_analysis.trainDecisionTree(facts: animalFacts, target: "category", features: ["wool", "horns", "milk", "weight"]),
    classified := fact_analysis.predict(model: classifier, input: {wool: 0.7, horns: 0.6, milk: 0.6, weight: 44}),
    weightModel := fact_analysis.trainLinearRegression(facts: animalFacts, target: "weight", feature: "milk"),
    predictedWeight := fact_analysis.predict(model: weightModel, input: {milk: 0.65}),
    goatSheep := fact.compareFacts(fact1: goat, fact2: sheep),
    goatWolf := fact.compareFacts(fact1: goat, fact2: wolf),
    nearestLivestock := fact_analysis.nearestFactsWhere(input: livestockNeed, candidates: animalFacts, count: 2, requiredFields: ["category", "diet", "habitat"]),
    generatedSynset := RuminantSynset(left: goat, right: sheep),
    return (
        floatArithmetic: floatArithmetic,
        numericStats: numericStats,
        profile: profile,
        clusters: clusters,
        classifier: classifier,
        classified: classified,
        predictedWeight: predictedWeight,
        goatSheep: goatSheep,
        goatWolf: goatWolf,
        nearestLivestock: nearestLivestock,
        generatedSynset: generatedSynset
    )
