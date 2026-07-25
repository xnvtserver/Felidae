import "wordnet.fx"

Synset(id: "entity.n.01", pos: "n")
Synset(id: "animal.n.01", pos: "n")
Synset(id: "cat.n.01", pos: "n")
Synset(id: "dog.n.01", pos: "n")
Synset(id: "bank.n.01", pos: "n")
Synset(id: "lender.n.01", pos: "n")

Lemma(id: "lemma.entity.en", text: "entity", language: "en")
Lemma(id: "lemma.animal.en", text: "animal", language: "en")
Lemma(id: "lemma.cat.en", text: "cat", language: "en")
Lemma(id: "lemma.feline.en", text: "feline", language: "en")
Lemma(id: "lemma.dog.en", text: "dog", language: "en")
Lemma(id: "lemma.canine.en", text: "canine", language: "en")
Lemma(id: "lemma.bank.en", text: "bank", language: "en")
Lemma(id: "lemma.lender.en", text: "lender", language: "en")
Lemma(id: "lemma.cat.es", text: "gato", language: "es")

Sense(id: "sense.entity.1", lemma: "lemma.entity.en", synset: "entity.n.01", number: 1, frequency: 50)
Sense(id: "sense.animal.1", lemma: "lemma.animal.en", synset: "animal.n.01", number: 1, frequency: 40)
Sense(id: "sense.cat.1", lemma: "lemma.cat.en", synset: "cat.n.01", number: 1, frequency: 30)
Sense(id: "sense.feline.1", lemma: "lemma.feline.en", synset: "cat.n.01", number: 2, frequency: 8)
Sense(id: "sense.dog.1", lemma: "lemma.dog.en", synset: "dog.n.01", number: 1, frequency: 28)
Sense(id: "sense.canine.1", lemma: "lemma.canine.en", synset: "dog.n.01", number: 2, frequency: 7)
Sense(id: "sense.bank.1", lemma: "lemma.bank.en", synset: "bank.n.01", number: 1, frequency: 12)
Sense(id: "sense.lender.1", lemma: "lemma.lender.en", synset: "lender.n.01", number: 1, frequency: 10)
Sense(id: "sense.cat.es.1", lemma: "lemma.cat.es", synset: "cat.n.01", number: 1, frequency: 4)

Gloss(synset: "entity.n.01", text: "something that exists")
Gloss(synset: "animal.n.01", text: "a living organism that moves and senses")
Gloss(synset: "cat.n.01", text: "small domesticated feline animal")
Gloss(synset: "dog.n.01", text: "domesticated canine animal loyal to people")
Gloss(synset: "bank.n.01", text: "financial institution that accepts deposits")
Gloss(synset: "lender.n.01", text: "financial person or institution that lends money")

Example(synset: "cat.n.01", text: "the kitten chased a toy")
Example(synset: "dog.n.01", text: "the dog guarded the house")
Example(synset: "bank.n.01", text: "the bank approved the loan")

Hypernym(child: "animal.n.01", parent: "entity.n.01")
Hypernym(child: "cat.n.01", parent: "animal.n.01")
Hypernym(child: "dog.n.01", parent: "animal.n.01")
Hypernym(child: "bank.n.01", parent: "entity.n.01")
Hypernym(child: "lender.n.01", parent: "entity.n.01")
SimilarTo(left: "bank.n.01", right: "lender.n.01")
Antonym(left: "cat.n.01", right: "dog.n.01")
MorphException(surface: "kittens", lemma: "cat")
ConceptFrequency(synset: "entity.n.01", count: 100)
ConceptFrequency(synset: "animal.n.01", count: 60)
ConceptFrequency(synset: "cat.n.01", count: 20)
ConceptFrequency(synset: "dog.n.01", count: 18)
ConceptFrequency(synset: "bank.n.01", count: 8)
ConceptFrequency(synset: "lender.n.01", count: 5)

WordNetCliSmoke(result: any) =>
    value := wordnet.check_similarity(word1: "cat", word2: "dog", algorithm: "path")
    return (result: value)

main() =>
    cat := {word: "cat"}
    dog := {word: "dog"}
    return (
        lookup: WordNetLookup(text: "cat"),
        lemmas: WordNetLemmas(text: "kittens"),
        synonyms: WordNetSynonyms(word: "cat"),
        ancestors: WordNetAncestors(synset: "cat.n.01"),
        descendants: WordNetDescendants(synset: "animal.n.01"),
        path: wordnet.shortest_path(left_synset: "cat.n.01", right_synset: "dog.n.01"),
        lcs: wordnet.lowest_common_subsumer(left_synset: "cat.n.01", right_synset: "dog.n.01"),
        pathSimilarity: wordnet.check_similarity(word1: "cat", word2: "dog", algorithm: "path"),
        wuPalmer: wordnet.check_similarity(word1: "cat", word2: "dog", algorithm: "wup"),
        resnik: wordnet.check_similarity(word1: "cat", word2: "dog", algorithm: "resnik"),
        lin: wordnet.check_similarity(word1: "cat", word2: "dog", algorithm: "lin"),
        factSimilarity: wordnet.check_similarity(fact1: cat, fact2: dog, algorithm: "path"),
        sense: WordNetSense(word: "bank", context: "the loan came from a financial institution"),
        expansion: WordNetExpand(query: "cat bank"),
        chains: wordnet.lexical_chains(text: "cat dog animal bank loan"),
        translation: WordNetTranslate(word: "cat", source_language: "en", target_language: "es")
    )
