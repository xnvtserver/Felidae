Animal(
    legs: 0,
    warm_blooded: false,
    diet: "unknown"
)

Mammal extend Animal(
    legs: 4,
    warm_blooded: true,
    diet: "mixed"
)

Tiger extend Mammal(
    species: "tiger",
    habitat: "wild"
)

Lion extend Mammal(
    species: "lion",
    habitat: "wild"
)

Cat extend Mammal(
    species: "cat",
    habitat: "domestic"
)

TigerFemale extend Tiger(
    name: "Shira",
    sex: "female",
    produces_milk: true,
    nurtures_young: true
)

TigerMale extend Tiger(
    name: "Raja",
    sex: "male",
    produces_milk: false,
    nurtures_young: false
)

CatFemale extend Cat(
    name: "Lilly",
    sex: "female",
    produces_milk: true,
    nurtures_young: true
)

CatMale extend Cat(
    name: "Sony",
    sex: "male",
    produces_milk: false,
    nurtures_young: false
)

PropertyEvidence(
    property: "",
    left_value: nil,
    right_value: nil,
    matched: false
)

AnimalSimilarityEvidence(
    left_type: "",
    right_type: "",
    common_ancestor: "Mammal",
    score: 0,
    matched_count: 0,
    compared_count: 0,
    matching_evidence: [],
    differing_evidence: []
)

buildAnimalEvidence(left: Mammal, right: Mammal) =>
    evidence := [
        PropertyEvidence(
            property: "legs",
            left_value: left.legs,
            right_value: right.legs,
            matched: left.legs == right.legs
        ),
        PropertyEvidence(
            property: "warm_blooded",
            left_value: left.warm_blooded,
            right_value: right.warm_blooded,
            matched: left.warm_blooded == right.warm_blooded
        ),
        PropertyEvidence(
            property: "diet",
            left_value: left.diet,
            right_value: right.diet,
            matched: left.diet == right.diet
        ),
        PropertyEvidence(
            property: "species",
            left_value: left.species,
            right_value: right.species,
            matched: left.species == right.species
        ),
        PropertyEvidence(
            property: "habitat",
            left_value: left.habitat,
            right_value: right.habitat,
            matched: left.habitat == right.habitat
        ),
        PropertyEvidence(
            property: "sex",
            left_value: left.sex,
            right_value: right.sex,
            matched: left.sex == right.sex
        ),
        PropertyEvidence(
            property: "produces_milk",
            left_value: left.produces_milk,
            right_value: right.produces_milk,
            matched: left.produces_milk == right.produces_milk
        ),
        PropertyEvidence(
            property: "nurtures_young",
            left_value: left.nurtures_young,
            right_value: right.nurtures_young,
            matched: left.nurtures_young == right.nurtures_young
        )
    ]
    matching := lambda(evidence, item => item.matched == true)
    differing := lambda(evidence, item => item.matched == false)
    matchedCount := count(matching)
    comparedCount := count(evidence)
    return AnimalSimilarityEvidence(
        left_type: type(left),
        right_type: type(right),
        common_ancestor: "Mammal",
        score: matchedCount / comparedCount,
        matched_count: matchedCount,
        compared_count: comparedCount,
        matching_evidence: matching,
        differing_evidence: differing
    )

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
    return buildAnimalEvidence(left: left, right: right)

main() =>
    tigerFemale := TigerFemale(
        name: "Shira",
        legs: 4,
        warm_blooded: true,
        diet: "carnivore",
        species: "tiger",
        habitat: "wild",
        sex: "female",
        produces_milk: true,
        nurtures_young: true
    )
    tigerMale := TigerMale(
        name: "Raja",
        legs: 4,
        warm_blooded: true,
        diet: "carnivore",
        species: "tiger",
        habitat: "wild",
        sex: "male",
        produces_milk: false,
        nurtures_young: false
    )
    catFemale := CatFemale(
        name: "Lilly",
        legs: 4,
        warm_blooded: true,
        diet: "carnivore",
        species: "cat",
        habitat: "domestic",
        sex: "female",
        produces_milk: true,
        nurtures_young: true
    )
    catMale := CatMale(
        name: "Sony",
        legs: 4,
        warm_blooded: true,
        diet: "carnivore",
        species: "cat",
        habitat: "domestic",
        sex: "male",
        produces_milk: false,
        nurtures_young: false
    )
    return (
        female_tiger_to_female_cat: tigerFemale similarTo catFemale,
        male_tiger_to_female_cat: tigerMale similarTo catFemale,
        male_tiger_to_male_cat: tigerMale similarTo catMale,
        male_tiger_to_female_tiger: tigerMale similarTo tigerFemale
    )
