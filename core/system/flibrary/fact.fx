# Native semantic fact declaration layer.
# User code should import "fact.fx" and call fact.* methods.
# These system.flibrary.fact.* declarations are reserved for the runtime
# native module bridge that invokes native_modules/fact.

system.flibrary.fact.extract_semantics(input: any) => ().
system.flibrary.fact.check_similarity(fact1: any, fact2: any, algorithm: string) => ().
system.flibrary.fact.check_similarity(fact1: any, fact2: any, algorithm: string, lexical_algorithm: string, field_alignment: string, collection_mode: string, missing_field_policy: string, threshold: number, maximum_depth: number, maximum_fields: number, explain: string) => ().
system.flibrary.fact.similarity_score(fact1: any, fact2: any) => ().
system.flibrary.fact.check_difference(fact1: any, fact2: any) => ().
system.flibrary.fact.check_difference(fact1: any, fact2: any, algorithm: string, explain: string) => ().
system.flibrary.fact.compare(fact1: any, fact2: any, algorithm: string) => ().
system.flibrary.fact.compare_facts(fact1: any, fact2: any) => ().
system.flibrary.fact.compare_facts(fact1: any, fact2: any, facts: array) => ().
system.flibrary.fact.match_pattern(fact: any, pattern: any, mode: string, threshold: number) => ().
system.flibrary.fact.near(fact1: any, fact2: any, threshold: number) => ().
system.flibrary.fact.align_fields(fact1: any, fact2: any, mode: string) => ().
system.flibrary.fact.semantic_unify(pattern: any, candidate: any, threshold: number) => ().
system.flibrary.fact.compare_properties(fact1: any, fact2: any) => ().
system.flibrary.fact.property_difference(fact1: any, fact2: any) => ().
system.flibrary.fact.common_ancestor(fact1: any, fact2: any) => ().
system.flibrary.fact.common_ancestor(fact1: any, fact2: any, facts: array) => ().
system.flibrary.fact.direct_relation(parent: any, child: any) => ().
system.flibrary.fact.is_ancestor(ancestor: any, descendant: any) => ().
system.flibrary.fact.is_descendant(descendant: any, ancestor: any) => ().
system.flibrary.fact.ancestor_closure(fact: any, facts: array) => ().
system.flibrary.fact.descendant_closure(fact: any, facts: array) => ().
system.flibrary.fact.shortest_path(fact1: any, fact2: any) => ().
system.flibrary.fact.shortest_path(fact1: any, fact2: any, facts: array) => ().
system.flibrary.fact.path_similarity(fact1: any, fact2: any, facts: array) => ().
system.flibrary.fact.wu_palmer_similarity(fact1: any, fact2: any, facts: array) => ().
system.flibrary.fact.resnik_similarity(fact1: any, fact2: any, facts: array) => ().
system.flibrary.fact.lin_similarity(fact1: any, fact2: any, facts: array) => ().
system.flibrary.fact.frequency_statistics(facts: array) => ().
system.flibrary.fact.normalize(text: string) => ().
system.flibrary.fact.aggregate_evidence(evidence: array) => ().
