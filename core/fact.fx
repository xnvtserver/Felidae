# Fact comparison and reasoning library.
# Importing this file makes fact.* APIs available, but does not change normal
# exact Felidae unification, substitution, backtracking, or fact lookup.

import ("flibrary", "system.flibrary.fact")

fact.extract_semantics(input: any) =>
    return (system_library_loader(module: "fact", function: "extract_semantics", args: {input: input}))

fact.check_similarity(fact1: any, fact2: any, algorithm: string) =>
    return (system_library_loader(module: "fact", function: "check_similarity", args: {fact1: fact1, fact2: fact2, algorithm: algorithm}))

fact.check_similarity(fact1: any, fact2: any, algorithm: string, lexical_algorithm: string, field_alignment: string, collection_mode: string, missing_field_policy: string, threshold: number, maximum_depth: number, maximum_fields: number, explain: bool) =>
    return (system_library_loader(module: "fact", function: "check_similarity", args: {fact1: fact1, fact2: fact2, algorithm: algorithm, lexical_algorithm: lexical_algorithm, field_alignment: field_alignment, collection_mode: collection_mode, missing_field_policy: missing_field_policy, threshold: threshold, maximum_depth: maximum_depth, maximum_fields: maximum_fields, explain: explain}))

fact.areSimilar(fact1: any, fact2: any, algorithm: string) =>
    return (fact.check_similarity(fact1: fact1, fact2: fact2, algorithm: "semantic_recursive", lexical_algorithm: algorithm, field_alignment: "semantic", collection_mode: "auto", missing_field_policy: "penalize", threshold: 0.80, maximum_depth: 32, maximum_fields: 256, explain: true))

fact.areSimilar(fact1: any, fact2: any, algorithm: string, threshold: number) =>
    return (fact.check_similarity(fact1: fact1, fact2: fact2, algorithm: "semantic_recursive", lexical_algorithm: algorithm, field_alignment: "semantic", collection_mode: "auto", missing_field_policy: "penalize", threshold: threshold, maximum_depth: 32, maximum_fields: 256, explain: true))

fact.similarity_score(fact1: any, fact2: any) =>
    return (system_library_loader(module: "fact", function: "similarity_score", args: {fact1: fact1, fact2: fact2}))

fact.check_difference(fact1: any, fact2: any) =>
    return (system_library_loader(module: "fact", function: "check_difference", args: {fact1: fact1, fact2: fact2}))

fact.check_difference(fact1: any, fact2: any, algorithm: string, explain: string) =>
    return (system_library_loader(module: "fact", function: "check_difference", args: {fact1: fact1, fact2: fact2, algorithm: algorithm, explain: explain}))

fact.compare(fact1: any, fact2: any, algorithm: string) =>
    return (system_library_loader(module: "fact", function: "compare", args: {fact1: fact1, fact2: fact2, algorithm: algorithm}))

fact.compare_facts(fact1: any, fact2: any) =>
    return (system_library_loader(module: "fact", function: "compare_facts", args: {fact1: fact1, fact2: fact2}))

fact.compare_facts(fact1: any, fact2: any, facts: array) =>
    return (system_library_loader(module: "fact", function: "compare_facts", args: {fact1: fact1, fact2: fact2, facts: facts}))

fact.compareFacts(fact1: any, fact2: any) =>
    return (fact.compare_facts(fact1: fact1, fact2: fact2))

fact.compareFacts(fact1: any, fact2: any, facts: array) =>
    return (fact.compare_facts(fact1: fact1, fact2: fact2, facts: facts))

fact.match_pattern(fact: any, pattern: any, mode: string, threshold: number) =>
    return (system_library_loader(module: "fact", function: "match_pattern", args: {fact: fact, pattern: pattern, mode: mode, threshold: threshold}))

fact.near(fact1: any, fact2: any, threshold: number) =>
    return (system_library_loader(module: "fact", function: "near", args: {fact1: fact1, fact2: fact2, threshold: threshold}))

fact.align_fields(fact1: any, fact2: any, mode: string) =>
    return (system_library_loader(module: "fact", function: "align_fields", args: {fact1: fact1, fact2: fact2, mode: mode}))

fact.semantic_unify(pattern: any, candidate: any, threshold: number) =>
    return (system_library_loader(module: "fact", function: "semantic_unify", args: {pattern: pattern, candidate: candidate, threshold: threshold}))

fact.compare_properties(fact1: any, fact2: any) =>
    return (system_library_loader(module: "fact", function: "compare_properties", args: {fact1: fact1, fact2: fact2}))

fact.compareProperties(fact1: any, fact2: any) =>
    return (fact.compare_properties(fact1: fact1, fact2: fact2))

fact.property_difference(fact1: any, fact2: any) =>
    return (system_library_loader(module: "fact", function: "property_difference", args: {fact1: fact1, fact2: fact2}))

fact.difference(fact1: any, fact2: any) =>
    return (fact.property_difference(fact1: fact1, fact2: fact2))

fact.common_ancestor(fact1: any, fact2: any) =>
    return (system_library_loader(module: "fact", function: "common_ancestor", args: {fact1: fact1, fact2: fact2}))

fact.common_ancestor(fact1: any, fact2: any, facts: array) =>
    return (system_library_loader(module: "fact", function: "common_ancestor", args: {fact1: fact1, fact2: fact2, facts: facts}))

fact.commonAncestor(fact1: any, fact2: any) =>
    return (fact.common_ancestor(fact1: fact1, fact2: fact2))

fact.commonAncestor(fact1: any, fact2: any, facts: array) =>
    return (fact.common_ancestor(fact1: fact1, fact2: fact2, facts: facts))

fact.direct_relation(parent: any, child: any) =>
    return (system_library_loader(module: "fact", function: "direct_relation", args: {parent: parent, child: child}))

fact.directRelation(parent: any, child: any) =>
    return (fact.direct_relation(parent: parent, child: child))

fact.is_ancestor(ancestor: any, descendant: any) =>
    return (system_library_loader(module: "fact", function: "is_ancestor", args: {ancestor: ancestor, descendant: descendant}))

fact.isAncestor(ancestor: any, descendant: any) =>
    return (fact.is_ancestor(ancestor: ancestor, descendant: descendant))

fact.is_descendant(descendant: any, ancestor: any) =>
    return (system_library_loader(module: "fact", function: "is_descendant", args: {descendant: descendant, ancestor: ancestor}))

fact.isDescendant(descendant: any, ancestor: any) =>
    return (fact.is_descendant(descendant: descendant, ancestor: ancestor))

fact.shortest_path(fact1: any, fact2: any) =>
    return (system_library_loader(module: "fact", function: "shortest_path", args: {fact1: fact1, fact2: fact2}))

fact.shortest_path(fact1: any, fact2: any, facts: array) =>
    return (system_library_loader(module: "fact", function: "shortest_path", args: {fact1: fact1, fact2: fact2, facts: facts}))

fact.shortestPath(fact1: any, fact2: any) =>
    return (fact.shortest_path(fact1: fact1, fact2: fact2))

fact.shortestPath(fact1: any, fact2: any, facts: array) =>
    return (fact.shortest_path(fact1: fact1, fact2: fact2, facts: facts))

fact.ancestor_closure(fact: any, facts: array) =>
    return (system_library_loader(module: "fact", function: "ancestor_closure", args: {fact: fact, facts: facts}))

fact.ancestorClosure(fact: any, facts: array) =>
    return (fact.ancestor_closure(fact: fact, facts: facts))

fact.descendant_closure(fact: any, facts: array) =>
    return (system_library_loader(module: "fact", function: "descendant_closure", args: {fact: fact, facts: facts}))

fact.descendantClosure(fact: any, facts: array) =>
    return (fact.descendant_closure(fact: fact, facts: facts))

fact.path_similarity(fact1: any, fact2: any, facts: array) =>
    return (system_library_loader(module: "fact", function: "path_similarity", args: {fact1: fact1, fact2: fact2, facts: facts}))

fact.pathSimilarity(fact1: any, fact2: any, facts: array) =>
    return (fact.path_similarity(fact1: fact1, fact2: fact2, facts: facts))

fact.wu_palmer_similarity(fact1: any, fact2: any, facts: array) =>
    return (system_library_loader(module: "fact", function: "wu_palmer_similarity", args: {fact1: fact1, fact2: fact2, facts: facts}))

fact.wuPalmerSimilarity(fact1: any, fact2: any, facts: array) =>
    return (fact.wu_palmer_similarity(fact1: fact1, fact2: fact2, facts: facts))

fact.resnik_similarity(fact1: any, fact2: any, facts: array) =>
    return (system_library_loader(module: "fact", function: "resnik_similarity", args: {fact1: fact1, fact2: fact2, facts: facts}))

fact.resnikSimilarity(fact1: any, fact2: any, facts: array) =>
    return (fact.resnik_similarity(fact1: fact1, fact2: fact2, facts: facts))

fact.lin_similarity(fact1: any, fact2: any, facts: array) =>
    return (system_library_loader(module: "fact", function: "lin_similarity", args: {fact1: fact1, fact2: fact2, facts: facts}))

fact.linSimilarity(fact1: any, fact2: any, facts: array) =>
    return (fact.lin_similarity(fact1: fact1, fact2: fact2, facts: facts))

fact.frequency_statistics(facts: array) =>
    return (system_library_loader(module: "fact", function: "frequency_statistics", args: {facts: facts}))

fact.frequencyStatistics(facts: array) =>
    return (fact.frequency_statistics(facts: facts))

fact.normalize(text: string) =>
    return (system_library_loader(module: "fact", function: "normalize", args: {text: text}))

fact.aggregate_evidence(evidence: array) =>
    return (system_library_loader(module: "fact", function: "aggregate_evidence", args: {evidence: evidence}))

fact.aggregateEvidence(evidence: array) =>
    return (fact.aggregate_evidence(evidence: evidence))

FactSimilarity(left: any, right: any) =>
    result := fact.check_similarity(fact1: left, fact2: right, algorithm: "semantic_recursive")
    return (result: result)

FactDifference(left: any, right: any) =>
    result := fact.check_difference(fact1: left, fact2: right)
    return (result: result)

FactPropertyComparison(left: any, right: any) =>
    result := fact.compare_properties(fact1: left, fact2: right)
    return (result: result)

FactCommonAncestor(left: any, right: any) =>
    result := fact.common_ancestor(fact1: left, fact2: right)
    return (result: result)

Near(left: any, right: any, threshold: number) =>
    result := fact.near(fact1: left, fact2: right, threshold: threshold)
    matched := result.matched
    matched == true
    return (score: result.score)
