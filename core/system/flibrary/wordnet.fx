# Native WordNet declaration layer.
# User code should import "wordnet.fx" and call wordnet.* methods.
# These system.flibrary.wordnet.* declarations are reserved for the runtime
# native module bridge that invokes native_modules/wordnet.

system.flibrary.wordnet.lookup(text: string, language: string, pos: string) => ()
system.flibrary.wordnet.lemmatize(text: string, language: string) => ()
system.flibrary.wordnet.traverse(synset: string, relation: string, direction: string, max_depth: number) => ()
system.flibrary.wordnet.closure(synset: string, relation: string, direction: string, max_depth: number) => ()
system.flibrary.wordnet.ancestors(synset: string, max_depth: number) => ()
system.flibrary.wordnet.descendants(synset: string, max_depth: number) => ()
system.flibrary.wordnet.shortest_path(left_synset: string, right_synset: string) => ()
system.flibrary.wordnet.common_ancestors(left_synset: string, right_synset: string) => ()
system.flibrary.wordnet.lowest_common_subsumer(left_synset: string, right_synset: string) => ()
system.flibrary.wordnet.similarity(left_synset: string, right_synset: string, algorithm: string) => ()
system.flibrary.wordnet.check_similarity(word1: string, word2: string, algorithm: string) => ()
system.flibrary.wordnet.check_similarity(fact1: any, fact2: any, algorithm: string) => ()
system.flibrary.wordnet.disambiguate(word: string, context: string, algorithm: string) => ()
system.flibrary.wordnet.lesk(word: string, context: string) => ()
system.flibrary.wordnet.personalized_pagerank(word: string, context: string) => ()
system.flibrary.wordnet.expand_query(query: string, max_depth: number) => ()
system.flibrary.wordnet.lexical_chains(text: string) => ()
system.flibrary.wordnet.translate(word: string, source_language: string, target_language: string) => ()
system.flibrary.wordnet.synonyms(word: string, language: string) => ()
