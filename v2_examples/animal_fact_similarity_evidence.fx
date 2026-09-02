Animal(
    name: "animal",
    legs: 0,
    warm_blooded: 0.0,
    diet: "unknown"
)

Mammal extend Animal(
    name: "mammal",
    legs: 4,
    warm_blooded: 1.0,
    diet: "mixed"
)

Tiger extend Mammal(
    name: "tiger",
    species: "tiger",
    habitat: "wild"
)

Lion extend Mammal(
    name: "lion",
    species: "lion",
    habitat: "wild"
)

Cat extend Mammal(
    name: "cat",
    species: "cat",
    habitat: "domestic"
)

TigerFemale extend Tiger(
    name: "Shira",
    diet: "carnivore",
    sex: "female",
    produces_milk: 1.0,
    nurtures_young: 1.0
)

TigerMale extend Tiger(
    name: "Raja",
    diet: "carnivore",
    sex: "male",
    produces_milk: 0.0,
    nurtures_young: 0.0
)

CatFemale extend Cat(
    name: "Lilly",
    diet: "carnivore",
    sex: "female",
    produces_milk: 1.0,
    nurtures_young: 1.0
)

CatMale extend Cat(
    name: "Sony",
    diet: "carnivore",
    sex: "male",
    produces_milk: 0.0,
    nurtures_young: 0.0
)

AnimalSimilarityEvidence(
    left_type: "",
    right_type: "",
    common_ancestor: nil,
    score: 0,
    ancestor_similarity: 0,
    property_similarity: 0,
    similar: 0.0,
    matched_properties: [],
    differing_properties: [],
    ancestor_distance: 0,
    ancestor_evidence: [],
    property_evidence: []
)

# One inherited projection defines which properties have meaning for this
# comparison. Native hierarchy and similarity operations supply evidence for
# every Mammal subtype without duplicating the comparison algorithm.
Mammal.membership(input: Mammal, against: Mammal) =>
    return {
        legs: input.legs,
        warm_blooded: input.warm_blooded,
        diet: input.diet,
        species: input.species,
        habitat: input.habitat,
        sex: input.sex,
        produces_milk: input.produces_milk,
        nurtures_young: input.nurtures_young
    }

@overload(
    operator: animalSimilarity,
    pattern: "{left} similarTo {right}",
    captures: {left: Mammal, right: Mammal},
    result: AnimalSimilarityEvidence,
    precedence: relationship,
    associativity: none,
    cardinality: one,
    effects: pure,
    visibility: private
)
compareMammals() =>
    ancestors := commonAncestors(left, right)
    score := similarity(left, right)
    return AnimalSimilarityEvidence(
        left_type: type(left),
        right_type: type(right),
        common_ancestors: ancestors,
        score: score,
        ancestor_similarity: array.len(data: ancestors) > 0,
        property_similarity: score,
        similar: score >= 0.60,
        matched_properties: [],
        differing_properties: [],
        ancestor_distance: 0.0,
        ancestor_evidence: ancestors,
        property_evidence: []
    )

main() =>
    tigerFemales := lambda(TigerFemale, animal => animal.name == "Shira")
    tigerMales := lambda(TigerMale, animal => animal.name == "Raja")
    catFemales := lambda(CatFemale, animal => animal.name == "Lilly")
    catMales := lambda(CatMale, animal => animal.name == "Sony")

    tigerFemale := array.get(data: tigerFemales, position: 0)
    tigerMale := array.get(data: tigerMales, position: 0)
    catFemale := array.get(data: catFemales, position: 0)
    catMale := array.get(data: catMales, position: 0)

    femalePair := tigerFemale similarTo catFemale
    mixedPair := tigerMale similarTo catFemale
    malePair := tigerMale similarTo catMale
    sameSpeciesPair := tigerMale similarTo tigerFemale

    return {
        evidence: [femalePair, mixedPair, malePair, sameSpeciesPair],
        decisions: {
            female_tiger_to_female_cat: femalePair.similar,
            male_tiger_to_female_cat: mixedPair.similar,
            male_tiger_to_male_cat: malePair.similar,
            male_tiger_to_female_tiger: sameSpeciesPair.similar
        }
    }
