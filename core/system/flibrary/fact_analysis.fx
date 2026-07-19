# Native fact analysis declaration layer.
# User code should import "fact_analysis.fx" and call fact_analysis.* methods.
# Model-producing methods return inspectable Felidae source text/facts, not
# binary model blobs.

system.flibrary.fact_analysis.find_nearest(input: any, candidates: array, count: number) => ().
system.flibrary.fact_analysis.find_nearest_where(input: any, candidates: array, count: number, required_fields: array) => ().
system.flibrary.fact_analysis.predict_next(facts: array, factType: string) => ().
system.flibrary.fact_analysis.train_decision_tree(facts: array, target: string, features: array) => ().
system.flibrary.fact_analysis.predict(model: any, input: any) => ().
system.flibrary.fact_analysis.apply_model(model: any, input: any) => ().
system.flibrary.fact_analysis.evaluate_model(model: any, facts: array, target: string) => ().
system.flibrary.fact_analysis.cluster_facts(facts: array, features: array, clusters: number) => ().
system.flibrary.fact_analysis.discover_associations(facts: array, min_support: number) => ().
system.flibrary.fact_analysis.profile_facts(facts: array, features: array) => ().
system.flibrary.fact_analysis.correlate_facts(facts: array, left: string, right: string) => ().
system.flibrary.fact_analysis.train_linear_regression(facts: array, target: string, feature: string) => ().
