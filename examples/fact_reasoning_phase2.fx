import ("fact.fx", "fact_analysis.fx").

Person(name: "Default", role: "person").
Employee extend Person(name: "Default", role: "employee").
Student extend Person(name: "Default", role: "student").

main() =>
    employee := {__type: "Employee", __parent: "Person", name: "Ravi", role: "Engineer", score: 82},
    student := {__type: "Student", __parent: "Person", name: "Maya", role: "Learner", score: 78},
    employee2 := {__type: "Employee", __parent: "Person", name: "Ravi", role: "Engineer", score: 90},
    person := {__type: "Person", name: "Default", role: "person"},
    propertyComparison := fact.compareProperties(fact1: employee, fact2: employee2),
    propertyDifference := fact.difference(fact1: employee, fact2: employee2),
    semanticSimilarity := fact.areSimilar(fact1: employee, fact2: employee2, algorithm: "Leacock-Chodorow"),
    common := fact.commonAncestor(fact1: employee, fact2: student),
    ancestor := fact.isAncestor(ancestor: person, descendant: employee),
    path := fact.shortestPath(fact1: employee, fact2: student),
    evidence := fact.aggregateEvidence(evidence: [
        {source: "rule.humidity", probability: 0.8, weight: 2},
        {source: "rule.temperature", probability: 0.6, weight: 1}
    ]),
    nearest := fact_analysis.nearestFacts(input: employee, candidates: [employee2, student], count: 1),
    next := fact_analysis.predictNext(facts: [
        {__type: "Climate", month: "August", humidity: 65, probabilityOfRain: 40, temperature: 34},
        {__type: "Climate", month: "September", humidity: 72, probabilityOfRain: 55, temperature: 31}
    ], factType: "ClimatePrediction"),
    model := fact_analysis.trainDecisionTree(facts: [
        {humidity: 65, temperature: 34, climate: "sunny"},
        {humidity: 72, temperature: 31, climate: "rainy"},
        {humidity: 75, temperature: 29, climate: "rainy"}
    ], target: "climate", features: ["humidity", "temperature"]),
    applied := fact_analysis.predict(model: model, input: {humidity: 74, temperature: 30}),
    evaluated := fact_analysis.evaluateModel(model: model, facts: [
        {humidity: 65, temperature: 34, climate: "sunny"},
        {humidity: 72, temperature: 31, climate: "rainy"},
        {humidity: 75, temperature: 29, climate: "rainy"}
    ], target: "climate"),
    savedModel := fact_analysis.saveModel(path: "build/generated_decision_tree_model.fx", model: model),
    return (
        propertyComparison: propertyComparison,
        propertyDifference: propertyDifference,
        semanticSimilarity: semanticSimilarity,
        common: common,
        ancestor: ancestor,
        path: path,
        evidence: evidence,
        nearest: nearest,
        next: next,
        model: model,
        applied: applied,
        evaluated: evaluated,
        savedModel: savedModel
    ).
