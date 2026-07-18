# WordNet semantic library.
# User code calls wordnet.* methods. The public methods use the internal
# flibrary loader, while system.flibrary.wordnet.* stays as the typed ABI
# contract for native_modules/wordnet.

import ("flibrary" "system.flibrary.wordnet").

wordnet.lookup(text: string, language: string, pos: string) =>
    return (system_library_loader(module: "wordnet", function: "lookup", args: {text: text, language: language, pos: pos})).

wordnet.lemmatize(text: string, language: string) =>
    return (system_library_loader(module: "wordnet", function: "lemmatize", args: {text: text, language: language})).

wordnet.traverse(synset: string, relation: string, direction: string, max_depth: number) =>
    return (system_library_loader(module: "wordnet", function: "traverse", args: {synset: synset, relation: relation, direction: direction, max_depth: max_depth})).

wordnet.closure(synset: string, relation: string, direction: string, max_depth: number) =>
    return (system_library_loader(module: "wordnet", function: "closure", args: {synset: synset, relation: relation, direction: direction, max_depth: max_depth})).

wordnet.ancestors(synset: string, max_depth: number) =>
    return (system_library_loader(module: "wordnet", function: "ancestors", args: {synset: synset, max_depth: max_depth})).

wordnet.descendants(synset: string, max_depth: number) =>
    return (system_library_loader(module: "wordnet", function: "descendants", args: {synset: synset, max_depth: max_depth})).

wordnet.shortest_path(left_synset: string, right_synset: string) =>
    return (system_library_loader(module: "wordnet", function: "shortest_path", args: {left_synset: left_synset, right_synset: right_synset})).

wordnet.common_ancestors(left_synset: string, right_synset: string) =>
    return (system_library_loader(module: "wordnet", function: "common_ancestors", args: {left_synset: left_synset, right_synset: right_synset})).

wordnet.lowest_common_subsumer(left_synset: string, right_synset: string) =>
    return (system_library_loader(module: "wordnet", function: "lowest_common_subsumer", args: {left_synset: left_synset, right_synset: right_synset})).

wordnet.similarity(left_synset: string, right_synset: string, algorithm: string) =>
    return (system_library_loader(module: "wordnet", function: "similarity", args: {left_synset: left_synset, right_synset: right_synset, algorithm: algorithm})).

wordnet.check_similarity(word1: string, word2: string, algorithm: string) =>
    return (system_library_loader(module: "wordnet", function: "check_similarity", args: {word1: word1, word2: word2, algorithm: algorithm})).

wordnet.check_similarity(fact1: any, fact2: any, algorithm: string) =>
    return (system_library_loader(module: "wordnet", function: "check_similarity", args: {fact1: fact1, fact2: fact2, algorithm: algorithm})).

wordnet.disambiguate(word: string, context: string, algorithm: string) =>
    return (system_library_loader(module: "wordnet", function: "disambiguate", args: {word: word, context: context, algorithm: algorithm})).

wordnet.lesk(word: string, context: string) =>
    return (system_library_loader(module: "wordnet", function: "lesk", args: {word: word, context: context})).

wordnet.personalized_pagerank(word: string, context: string) =>
    return (system_library_loader(module: "wordnet", function: "personalized_pagerank", args: {word: word, context: context})).

wordnet.expand_query(query: string, max_depth: number) =>
    return (system_library_loader(module: "wordnet", function: "expand_query", args: {query: query, max_depth: max_depth})).

wordnet.lexical_chains(text: string) =>
    return (system_library_loader(module: "wordnet", function: "lexical_chains", args: {text: text})).

wordnet.translate(word: string, source_language: string, target_language: string) =>
    return (system_library_loader(module: "wordnet", function: "translate", args: {word: word, source_language: source_language, target_language: target_language})).

wordnet.synonyms(word: string, language: string) =>
    return (system_library_loader(module: "wordnet", function: "synonyms", args: {word: word, language: language})).

WordNetLookup(text: string) =>
    return (wordnet.lookup(text: text, language: "en", pos: "")).

WordNetLemmas(text: string) =>
    return (wordnet.lemmatize(text: text, language: "en")).

WordNetSynonyms(word: string) =>
    return (wordnet.synonyms(word: word, language: "en")).

WordNetAncestors(synset: string) =>
    return (wordnet.ancestors(synset: synset, max_depth: 32)).

WordNetDescendants(synset: string) =>
    return (wordnet.descendants(synset: synset, max_depth: 32)).

WordNetDistance(left: string, right: string) =>
    return (wordnet.shortest_path(left_synset: left, right_synset: right)).

WordNetSimilarity(left: string, right: string, algorithm: string) =>
    return (wordnet.similarity(left_synset: left, right_synset: right, algorithm: algorithm)).

WordNetWordSimilarity(left: string, right: string, algorithm: string) =>
    return (wordnet.check_similarity(word1: left, word2: right, algorithm: algorithm)).

WordNetCheckSimilarity(left: string, right: string, algorithm: string) =>
    result := wordnet.check_similarity(word1: left, word2: right, algorithm: algorithm),
    return (result: result).

WordNetSense(word: string, context: string) =>
    return (wordnet.disambiguate(word: word, context: context, algorithm: "lesk")).

WordNetExpand(query: string) =>
    return (wordnet.expand_query(query: query, max_depth: 1)).

WordNetTranslate(word: string, source_language: string, target_language: string) =>
    return (wordnet.translate(word: word, source_language: source_language, target_language: target_language)).
