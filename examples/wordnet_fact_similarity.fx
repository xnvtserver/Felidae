import ("array", "file", "http", "json", "probability")

# The Open English WordNet project publishes current English WordNet releases
# in JSON form. This method downloads a JSON resource into the local workspace.
DownloadWordNetJson(url: string, path: string) =>
    body := http.get(url: url)
    writeStatus := file.writeFile(path: path, data: body, mode: "write")
    parsed := json.parse(data: body)
    keys := json.keys(data: parsed)
    return (
        path: path,
        write: writeStatus,
        top_level_keys: keys
    )

# Mini WordNet-like facts used by the runnable comparison below. A full
# downloaded WordNet JSON can be transformed into facts with the same shape.
WordNetSense(word: "kitten", synset: "cat.n.01", semantic: "feline", related: "cat", related: "young")
WordNetSense(word: "tiger", synset: "tiger.n.02", semantic: "feline", related: "cat", related: "wild")
WordNetSense(word: "lilly", synset: "name.n.01", semantic: "given-name", related: "person")
WordNetSense(word: "sony", synset: "brand.n.01", semantic: "brand", related: "company")
WordNetSense(word: "joy", synset: "feeling.n.01", semantic: "emotion", related: "happy")
WordNetSense(word: "teddy", synset: "toy.n.01", semantic: "toy", related: "bear")
WordNetSense(word: "snowbell", synset: "cat.n.01", semantic: "feline", related: "cat", related: "pet")

Cat1(name: "kitten", name: "tiger", name: "lilly", name: "sony")
Cat2(name: "joy", name: "teddy", name: "snowbell")

WordScore(left: string, right: string) =>
    leftSense := Fact.first(type: "WordNetSense", field: "word", equals: left)
    rightSense := Fact.first(type: "WordNetSense", field: "word", equals: right)
    where leftSense.semantic == rightSense.semantic
    return (
        left: left,
        right: right,
        probability: 1,
        relation: leftSense.semantic
    )
else
    return (
        left: left,
        right: right,
        probability: 0,
        relation: "different"
    )

ScoresAgainst(right: string) =>
    cat1Rows := Fact.all(type: "Cat1")
    cat1 := array.get(data: cat1Rows, index: 0)
    scores := lambda(cat1.name, leftName => WordScore(left: leftName, right: right))
    return (right: right, scores: scores)

main() =>
    cat1Rows := Fact.all(type: "Cat1")
    cat2Rows := Fact.all(type: "Cat2")
    cat1 := array.get(data: cat1Rows, index: 0)
    cat2 := array.get(data: cat2Rows, index: 0)
    joyScores := ScoresAgainst(right: "joy")
    teddyScores := ScoresAgainst(right: "teddy")
    snowbellScores := ScoresAgainst(right: "snowbell")
    joyMatches := count(lambda(joyScores.scores, score => score.probability == 1))
    teddyMatches := count(lambda(teddyScores.scores, score => score.probability == 1))
    snowbellMatches := count(lambda(snowbellScores.scores, score => score.probability == 1))
    matches := joyMatches + teddyMatches + snowbellMatches
    pairCount := count(cat1.name) * count(cat2.name)
    similarityProbability := matches / pairCount
    differenceProbability := 1 - similarityProbability
    return (
        left_fact: cat1,
        right_fact: cat2,
        pair_scores: [joyScores, teddyScores, snowbellScores],
        probabilities: [
            {metric: "wordnet_semantic_similarity", value: similarityProbability},
            {metric: "wordnet_semantic_difference", value: differenceProbability}
        ],
        matched_pairs: matches,
        compared_pairs: pairCount
    )
