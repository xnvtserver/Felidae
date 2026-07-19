# Native ML stdlib declarations and fact-mining helpers.

import ("flibrary", "system.flibrary.fact_analysis", "file").

ml.sigmoid(value: number) => ().
ml.relu(value: number) => ().
ml.dot(left: array, right: array) => ().
ml.meanSquaredError(left: array, right: array) => ().

ml.cluster_facts(facts: array, features: array, clusters: number) =>
    return (system_library_loader(module: "fact_analysis", function: "cluster_facts", args: {facts: facts, features: features, clusters: clusters})).

ml.discover_associations(facts: array, min_support: number) =>
    return (system_library_loader(module: "fact_analysis", function: "discover_associations", args: {facts: facts, min_support: min_support})).

ml.profile_facts(facts: array, features: array) =>
    return (system_library_loader(module: "fact_analysis", function: "profile_facts", args: {facts: facts, features: features})).

ml.correlate_facts(facts: array, left: string, right: string) =>
    return (system_library_loader(module: "fact_analysis", function: "correlate_facts", args: {facts: facts, left: left, right: right})).

ml.train_decision_tree(facts: array, target: string, features: array) =>
    return (system_library_loader(module: "fact_analysis", function: "train_decision_tree", args: {facts: facts, target: target, features: features})).

ml.train_linear_regression(facts: array, target: string, feature: string) =>
    return (system_library_loader(module: "fact_analysis", function: "train_linear_regression", args: {facts: facts, target: target, feature: feature})).

ml.predict(model: any, input: any) =>
    return (system_library_loader(module: "fact_analysis", function: "predict", args: {model: model, input: input})).

ml.predict_fact(facts: array, input: any, target: string, features: array) =>
    model := ml.train_decision_tree(facts: facts, target: target, features: features),
    prediction := ml.predict(model: model, input: input),
    return (prediction: prediction, model: model).

ml.predict_numeric_fact(facts: array, input: any, target: string, feature: string) =>
    model := ml.train_linear_regression(facts: facts, target: target, feature: feature),
    prediction := ml.predict(model: model, input: input),
    return (prediction: prediction, model: model).

ml.save_model(path: string, model: any) =>
    return (file.writeFile(path: path, data: model.model_fx, mode: "write")).
