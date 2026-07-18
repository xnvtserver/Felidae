import ("fact.fx", "fact_analysis.fx", "ml").

Vegetable(name: "Tomato", category: "vegetable", texture: "soft", sweetness: 4, acidity: 8, umami: 7, heat: 0, cookMinutes: 12, role: "base").
Vegetable(name: "Carrot", category: "vegetable", texture: "firm", sweetness: 7, acidity: 1, umami: 3, heat: 0, cookMinutes: 18, role: "body").
Vegetable(name: "Spinach", category: "vegetable", texture: "leafy", sweetness: 2, acidity: 1, umami: 5, heat: 0, cookMinutes: 5, role: "green").
Fruit(name: "Mango", category: "fruit", texture: "soft", sweetness: 9, acidity: 3, umami: 1, heat: 0, cookMinutes: 0, role: "sweetness").
Sauce(name: "YogurtSauce", category: "sauce", texture: "creamy", sweetness: 2, acidity: 6, umami: 3, heat: 0, cookMinutes: 0, role: "cooling").
Sauce(name: "ChiliSauce", category: "sauce", texture: "glossy", sweetness: 3, acidity: 5, umami: 4, heat: 9, cookMinutes: 0, role: "heat").
Seasoning(name: "Cumin", category: "seasoning", texture: "powder", sweetness: 1, acidity: 0, umami: 6, heat: 2, cookMinutes: 2, role: "aroma").
Seasoning(name: "Mint", category: "seasoning", texture: "leafy", sweetness: 1, acidity: 1, umami: 2, heat: 0, cookMinutes: 0, role: "freshness").

DishHistory(name: "TomatoCurry", meal: "lunch", dishType: "meal", cookMinutes: 35, sweetness: 3, acidity: 7, umami: 8, heat: 6).
DishHistory(name: "MangoMintBowl", meal: "dinner", dishType: "dessert", cookMinutes: 8, sweetness: 9, acidity: 3, umami: 1, heat: 0).
DishHistory(name: "SpinachCarrotSoup", meal: "dinner", dishType: "starter", cookMinutes: 25, sweetness: 4, acidity: 1, umami: 6, heat: 2).
DishHistory(name: "ChiliTomatoStarter", meal: "lunch", dishType: "starter", cookMinutes: 18, sweetness: 3, acidity: 6, umami: 6, heat: 8).

CookingMethod(name: "QuickSaute", cookTime: "short", textureResult: "bright", bestFor: "starter").
CookingMethod(name: "SlowSimmer", cookTime: "long", textureResult: "deep", bestFor: "meal").
CookingMethod(name: "FreshFold", cookTime: "short", textureResult: "fresh", bestFor: "dessert").

CookingRequest(meal: string, cookMinutes: number, sweetness: number, acidity: number, umami: number, heat: number, preferredCategory: string, maxIngredientMinutes: number, ingredientRole: string) =>
    return ({meal: meal, cookMinutes: cookMinutes, sweetness: sweetness, acidity: acidity, umami: umami, heat: heat, preferredCategory: preferredCategory, maxIngredientMinutes: maxIngredientMinutes, ingredientRole: ingredientRole}).

CookingIngredients() =>
    return ([
        {__type: "Vegetable", name: "Tomato", category: "vegetable", texture: "soft", sweetness: 4, acidity: 8, umami: 7, heat: 0, cookMinutes: 12, role: "base"},
        {__type: "Vegetable", name: "Carrot", category: "vegetable", texture: "firm", sweetness: 7, acidity: 1, umami: 3, heat: 0, cookMinutes: 18, role: "body"},
        {__type: "Vegetable", name: "Spinach", category: "vegetable", texture: "leafy", sweetness: 2, acidity: 1, umami: 5, heat: 0, cookMinutes: 5, role: "green"},
        {__type: "Fruit", name: "Mango", category: "fruit", texture: "soft", sweetness: 9, acidity: 3, umami: 1, heat: 0, cookMinutes: 0, role: "sweetness"},
        {__type: "Sauce", name: "YogurtSauce", category: "sauce", texture: "creamy", sweetness: 2, acidity: 6, umami: 3, heat: 0, cookMinutes: 0, role: "cooling"},
        {__type: "Sauce", name: "ChiliSauce", category: "sauce", texture: "glossy", sweetness: 3, acidity: 5, umami: 4, heat: 9, cookMinutes: 0, role: "heat"},
        {__type: "Seasoning", name: "Cumin", category: "seasoning", texture: "powder", sweetness: 1, acidity: 0, umami: 6, heat: 2, cookMinutes: 2, role: "aroma"},
        {__type: "Seasoning", name: "Mint", category: "seasoning", texture: "leafy", sweetness: 1, acidity: 1, umami: 2, heat: 0, cookMinutes: 0, role: "freshness"}
    ]).

DishTrainingFacts() =>
    return ([
        {name: "TomatoCurry", meal: "lunch", dishType: "meal", cookMinutes: 35, sweetness: 3, acidity: 7, umami: 8, heat: 6},
        {name: "MangoMintBowl", meal: "dinner", dishType: "dessert", cookMinutes: 8, sweetness: 9, acidity: 3, umami: 1, heat: 0},
        {name: "SpinachCarrotSoup", meal: "dinner", dishType: "starter", cookMinutes: 25, sweetness: 4, acidity: 1, umami: 6, heat: 2},
        {name: "ChiliTomatoStarter", meal: "lunch", dishType: "starter", cookMinutes: 18, sweetness: 3, acidity: 6, umami: 6, heat: 8}
    ]).

IngredientNeedFromRequest(request: any) =>
    return ({category: request.preferredCategory, sweetness: request.sweetness, acidity: request.acidity, umami: request.umami, heat: request.heat, cookMinutes: request.maxIngredientMinutes, role: request.ingredientRole}).

IngredientFitness(ingredient: any, need: any) =>
    similarity := fact.compareProperties(fact1: ingredient, fact2: need),
    difference := fact.difference(fact1: ingredient, fact2: need),
    return (ingredient: ingredient.name, score: similarity.score, matchedFields: similarity.matched_fields, differences: difference.differences, justification: "score comes from shared key-value properties and listed differences").

ChooseIngredients(request: any) =>
    ingredients := CookingIngredients(),
    need := IngredientNeedFromRequest(request: request),
    nearest := fact_analysis.nearestFactsWhere(input: need, candidates: ingredients, count: 4, requiredFields: ["category", "role"]),
    return (need: need, ranked: nearest).

ClassifyDish(request: any) =>
    history := DishTrainingFacts(),
    model := ml.train_decision_tree(facts: history, target: "dishType", features: ["cookMinutes", "sweetness", "acidity", "umami", "heat"]),
    prediction := ml.predict(model: model, input: request),
    return (model: model, prediction: prediction).

ExplainDishProposal(request: any) =>
    ingredients := CookingIngredients(),
    tomato := {__type: "Vegetable", name: "Tomato", category: "vegetable", texture: "soft", sweetness: 4, acidity: 8, umami: 7, heat: 0, cookMinutes: 12, role: "base"},
    ingredientChoices := ChooseIngredients(request: request),
    dishClass := ClassifyDish(request: request),
    tomatoFitness := IngredientFitness(ingredient: tomato, need: ingredientChoices.need),
    profile := ml.profile_facts(facts: ingredients, features: ["sweetness", "acidity", "umami", "heat", "cookMinutes"]),
    clusters := ml.cluster_facts(facts: ingredients, features: ["sweetness", "acidity", "umami", "heat", "cookMinutes"], clusters: 3),
    associations := ml.discover_associations(facts: DishTrainingFacts(), min_support: 0.5),
    evidence := fact.aggregateEvidence(evidence: [
        {source: "tomato_base_property_fit", probability: tomatoFitness.score, weight: 3},
        {source: "dish_type_classifier", probability: dishClass.prediction.confidence, weight: 2}
    ]),
    return (
        request: request,
        predictedDishType: dishClass.prediction,
        ingredientNeed: ingredientChoices.need,
        rankedIngredients: ingredientChoices.ranked,
        ingredientFitnessExample: tomatoFitness,
        ingredientClusters: clusters,
        ingredientProfile: profile,
        dishAssociations: associations,
        evidence: evidence,
        justification: "The proposal is based on property similarity, key-value differences, learned dish type, ingredient clusters, and weighted evidence."
    ).

ProposeDish(meal: string, cookMinutes: number, sweetness: number, acidity: number, umami: number, heat: number, preferredCategory: string, maxIngredientMinutes: number, ingredientRole: string) =>
    request := CookingRequest(meal: meal, cookMinutes: cookMinutes, sweetness: sweetness, acidity: acidity, umami: umami, heat: heat, preferredCategory: preferredCategory, maxIngredientMinutes: maxIngredientMinutes, ingredientRole: ingredientRole),
    return (ExplainDishProposal(request: request)).

main() =>
    proposal := ProposeDish(meal: "lunch", cookMinutes: 20, sweetness: 3, acidity: 6, umami: 7, heat: 7, preferredCategory: "vegetable", maxIngredientMinutes: 15, ingredientRole: "base"),
    return (proposal: proposal).
