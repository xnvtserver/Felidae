import "wordnet.fx".

Synset(id: "cat.n.01", pos: "n").
Lemma(id: "lemma.cat.en", text: "cat", language: "en").
Sense(id: "sense.cat.1", lemma: "lemma.cat.en", synset: "cat.n.01", number: 1, frequency: 10).
MorphException(surface: "kittens", lemma: "cat").

main() =>
    lemmas := wordnet.lemmatize(text: "kittens", language: "en"),
    return (lemmas: lemmas).
