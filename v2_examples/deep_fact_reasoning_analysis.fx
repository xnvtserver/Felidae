# A deliberately non-trivial knowledge graph.  It contains overlapping
# taxonomic paths, concrete facts with contradictory local evidence, and a
# relationship whose membership is directional by construction.

# Declaring one placeholder instance registers "Relationship" as a known
# fact type, the same way every other type below does -- required for
# lambda(Relationship, ...) to be recognized as fact-query sugar rather than
# an unrecognized bare lambda call.
Relationship(from: nil, to: nil, name: "", degree: 0, confidence: 0)

Living(name: "living")
Animal extend Living(name: "animal", cellular: 1.0)
WarmBlooded extend Animal(name: "warm", temperature: "regulated")
FourLegged extend Animal(name: "four-legged", legs: 4)
Mammal extend WarmBlooded, FourLegged(name: "mammal", nourishes_young: 1.0)

Feline extend Mammal(name: "feline", family: "feline", diet: "carnivore")
Carnivore extend Mammal(name: "carnivore", diet: "carnivore", hunts: 1.0)
Domestic extend Mammal(name: "domestic", habitat: "home")
Wild extend Mammal(name: "wild", habitat: "wild")
Nocturnal extend Mammal(name: "nocturnal", active_time: "night")

TigerFemale extend Feline, Carnivore, Wild, Nocturnal(
    name: "shira",
    species: "tiger",
    sex: "female",
    produces_milk: 1.0,
    territory: "forest",
    hunts: 1.0
)

TigerMale extend Feline, Carnivore, Wild, Nocturnal(
    name: "raja",
    species: "tiger",
    sex: "male",
    produces_milk: 0.0,
    territory: "forest",
    hunts: 1.0
)

CatFemale extend Feline, Carnivore, Domestic(
    name: "lilly",
    species: "cat",
    sex: "female",
    produces_milk: 1.0,
    territory: "home",
    hunts: 0.0
)

DogMale extend Carnivore, Domestic(
    name: "max",
    species: "dog",
    sex: "male",
    produces_milk: 0.0,
    territory: "yard",
    hunts: 0.0
)

# The source fact owns membership knowledge, so comparison is directional:
# relating tiger to cat does not also relate cat to tiger. Querying both
# directions with plain field-equality lambdas makes that explicit instead of
# hiding it behind a comparison engine.
TigerFemale.membership(input: TigerFemale, against: Mammal) =>
    return {
        legs: input.legs,
        diet: input.diet,
        sex: input.sex,
        produces_milk: input.produces_milk,
        hunts: input.hunts,
        territory: input.territory
    }

CatFemale.membership(input: CatFemale, against: Mammal) =>
    return {
        legs: input.legs,
        diet: input.diet,
        sex: input.sex,
        produces_milk: input.produces_milk,
        habitat: input.habitat
    }

main() =>
    tiger_females := lambda(TigerFemale, fact => fact.name == "shira")
    tiger_males := lambda(TigerMale, fact => fact.name == "raja")
    cat_females := lambda(CatFemale, fact => fact.name == "lilly")
    dog_males := lambda(DogMale, fact => fact.name == "max")
    wild_families := lambda(Wild, fact => fact.name == "wild")

    tiger_female := array.get(data: tiger_females, position: 0)
    tiger_male := array.get(data: tiger_males, position: 0)
    cat_female := array.get(data: cat_females, position: 0)
    dog_male := array.get(data: dog_males, position: 0)
    wild := array.get(data: wild_families, position: 0)

    # A relationship is just an ordinary fact -- constructing one retains it
    # immediately, the same as any other fact literal -- not a special
    # relate() verb. from/to/degree/confidence are plain fields, queryable
    # the same way as any other fact type.
    tiger_cat_relationship := Relationship(
        from: tiger_female,
        to: cat_female,
        name: "shared-carnivore-evidence",
        degree: 0.78,
        confidence: 0.91
    )

    tied_lineage := commonAncestors(left: tiger_female, right: cat_female)
    broad_lineage := tied_lineage
    tiger_cat_lineage := ancestorAnalysis(left: tiger_female, right: cat_female)
    tiger_dog_lineage := commonAncestors(left: tiger_male, right: dog_male)
    habitat_propagation := propagateFact(
        parent: wild,
        child: tiger_female,
        changes: {habitat: "reserve", name: "wild-animal"}
    )

    # Directed vs. symmetric relationship lookup is a plain field-equality
    # query over the Relationship facts above -- the same lambda-based
    # filtering every other fact type already uses, not a bespoke
    # comparison engine.
    directed_tiger_cat := lambda(Relationship,
        r => r.from == tiger_female and r.to == cat_female)
    directed_cat_tiger := lambda(Relationship,
        r => r.from == cat_female and r.to == tiger_female)
    symmetric_tiger_cat := lambda(Relationship,
        r => (r.from == tiger_female and r.to == cat_female) or
             (r.from == cat_female and r.to == tiger_female))

    return DeepReasoningReport(
        tied_lineage: tied_lineage,
        broad_lineage: broad_lineage,
        tiger_cat_lineage: tiger_cat_lineage,
        tiger_dog_lineage: tiger_dog_lineage,
        habitat_propagation: habitat_propagation,
        directed_tiger_cat: directed_tiger_cat,
        directed_cat_tiger: directed_cat_tiger,
        symmetric_tiger_cat: symmetric_tiger_cat
    )
