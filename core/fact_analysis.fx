# Fact analysis and prediction library.
# This library builds on fact comparison primitives but keeps learning and
# prediction separate from the core fact-to-fact comparison API.

import ("flibrary", "system.flibrary.fact_analysis", "file").

fact_analysis.find_nearest(input: any, candidates: array, count: number) =>
    return (system_library_loader(module: "fact_analysis", function: "find_nearest", args: {input: input, candidates: candidates, count: count})).

fact_analysis.find_nearest_where(input: any, candidates: array, count: number, requiredFields: array) =>
    return (system_library_loader(module: "fact_analysis", function: "find_nearest_where", args: {input: input, candidates: candidates, count: count, required_fields: requiredFields})).

fact_analysis.nearestFacts(input: any, candidates: array, count: number) =>
    return (fact_analysis.find_nearest(input: input, candidates: candidates, count: count)).

fact_analysis.nearestFactsWhere(input: any, candidates: array, count: number, requiredFields: array) =>
    return (fact_analysis.find_nearest_where(input: input, candidates: candidates, count: count, requiredFields: requiredFields)).

fact_analysis.predict_next(facts: array, factType: string) =>
    return (system_library_loader(module: "fact_analysis", function: "predict_next", args: {facts: facts, factType: factType})).

fact_analysis.predictNext(facts: array, factType: string) =>
    return (fact_analysis.predict_next(facts: facts, factType: factType)).

fact_analysis.train_decision_tree(facts: array, target: string, features: array) =>
    return (system_library_loader(module: "fact_analysis", function: "train_decision_tree", args: {facts: facts, target: target, features: features})).

fact_analysis.trainDecisionTree(facts: array, target: string, features: array) =>
    return (fact_analysis.train_decision_tree(facts: facts, target: target, features: features)).

fact_analysis.predict(model: any, input: any) =>
    return (system_library_loader(module: "fact_analysis", function: "predict", args: {model: model, input: input})).

fact_analysis.apply_model(model: any, input: any) =>
    return (system_library_loader(module: "fact_analysis", function: "apply_model", args: {model: model, input: input})).

fact_analysis.evaluate_model(model: any, facts: array, target: string) =>
    return (system_library_loader(module: "fact_analysis", function: "evaluate_model", args: {model: model, facts: facts, target: target})).

fact_analysis.evaluateModel(model: any, facts: array, target: string) =>
    return (fact_analysis.evaluate_model(model: model, facts: facts, target: target)).

fact_analysis.cluster_facts(facts: array, features: array, clusters: number) =>
    return (system_library_loader(module: "fact_analysis", function: "cluster_facts", args: {facts: facts, features: features, clusters: clusters})).

fact_analysis.clusterFacts(facts: array, features: array, clusters: number) =>
    return (fact_analysis.cluster_facts(facts: facts, features: features, clusters: clusters)).

fact_analysis.discover_associations(facts: array, min_support: number) =>
    return (system_library_loader(module: "fact_analysis", function: "discover_associations", args: {facts: facts, min_support: min_support})).

fact_analysis.discoverAssociations(facts: array, minSupport: number) =>
    return (fact_analysis.discover_associations(facts: facts, min_support: minSupport)).

fact_analysis.profile_facts(facts: array, features: array) =>
    return (system_library_loader(module: "fact_analysis", function: "profile_facts", args: {facts: facts, features: features})).

fact_analysis.profileFacts(facts: array, features: array) =>
    return (fact_analysis.profile_facts(facts: facts, features: features)).

fact_analysis.correlate_facts(facts: array, left: string, right: string) =>
    return (system_library_loader(module: "fact_analysis", function: "correlate_facts", args: {facts: facts, left: left, right: right})).

fact_analysis.correlateFacts(facts: array, left: string, right: string) =>
    return (fact_analysis.correlate_facts(facts: facts, left: left, right: right)).

fact_analysis.train_linear_regression(facts: array, target: string, feature: string) =>
    return (system_library_loader(module: "fact_analysis", function: "train_linear_regression", args: {facts: facts, target: target, feature: feature})).

fact_analysis.trainLinearRegression(facts: array, target: string, feature: string) =>
    return (fact_analysis.train_linear_regression(facts: facts, target: target, feature: feature)).

fact_analysis.save_model(path: string, model: any) =>
    return (file.writeFile(path: path, data: model.model_fx, mode: "write")).

fact_analysis.saveModel(path: string, model: any) =>
    return (fact_analysis.save_model(path: path, model: model)).
