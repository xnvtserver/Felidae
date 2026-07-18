import "ml".

main() =>
    customers := [
        {__type: "Customer", name: "Asha", age: 24, spend: 120, visits: 3, segment: "starter", region: "south"},
        {__type: "Customer", name: "Mira", age: 27, spend: 160, visits: 4, segment: "starter", region: "south"},
        {__type: "Customer", name: "Nikhil", age: 42, spend: 780, visits: 12, segment: "premium", region: "west"},
        {__type: "Customer", name: "Dev", age: 45, spend: 820, visits: 13, segment: "premium", region: "west"}
    ],
    profile := ml.profile_facts(facts: customers, features: ["age", "spend", "visits"]),
    spendVisitCorrelation := ml.correlate_facts(facts: customers, left: "visits", right: "spend"),
    clusters := ml.cluster_facts(facts: customers, features: ["age", "spend", "visits"], clusters: 2),
    associations := ml.discover_associations(facts: customers, min_support: 0.5),
    model := ml.train_decision_tree(facts: customers, target: "segment", features: ["age", "spend", "visits"]),
    spendModel := ml.train_linear_regression(facts: customers, target: "spend", feature: "visits"),
    predicted := ml.predict(model: model, input: {age: 44, spend: 810, visits: 11}),
    predictedSpend := ml.predict(model: spendModel, input: {visits: 10}),
    factPrediction := ml.predict_fact(facts: customers, input: {age: 26, spend: 140, visits: 3}, target: "segment", features: ["age", "spend", "visits"]),
    numericPrediction := ml.predict_numeric_fact(facts: customers, input: {visits: 5}, target: "spend", feature: "visits"),
    savedModel := ml.save_model(path: "build/generated_customer_segment_model.fx", model: model),
    savedRegression := ml.save_model(path: "build/generated_customer_spend_model.fx", model: spendModel),
    return (
        profile: profile,
        spendVisitCorrelation: spendVisitCorrelation,
        clusters: clusters,
        associations: associations,
        model: model,
        spendModel: spendModel,
        predicted: predicted,
        predictedSpend: predictedSpend,
        factPrediction: factPrediction,
        numericPrediction: numericPrediction,
        savedModel: savedModel,
        savedRegression: savedRegression
    ).
