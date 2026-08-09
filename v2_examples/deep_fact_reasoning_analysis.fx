# A deliberately non-trivial knowledge graph.  It contains overlapping
# taxonomic paths, concrete facts with contradictory local evidence, and a
# comparison contract whose source membership is directional.

Living(name: "living")
Animal extend Living(name: "animal", cellular: true)
WarmBlooded extend Animal(name: "warm", temperature: "regulated")
FourLegged extend Animal(name: "four-legged", legs: 4)
Mammal extend WarmBlooded, FourLegged(name: "mammal", nourishes_young: true)

Feline extend Mammal(name: "feline", family: "feline", diet: "carnivore")
Carnivore extend Mammal(name: "carnivore", diet: "carnivore", hunts: true)
Domestic extend Mammal(name: "domestic", habitat: "home")
Wild extend Mammal(name: "wild", habitat: "wild")
Nocturnal extend Mammal(name: "nocturnal", active_time: "night")

TigerFemale extend Feline, Carnivore, Wild, Nocturnal(
    name: "shira",
    species: "tiger",
    sex: "female",
    produces_milk: true,
    territory: "forest",
    hunts: true
)

TigerMale extend Feline, Carnivore, Wild, Nocturnal(
    name: "raja",
    species: "tiger",
    sex: "male",
    produces_milk: false,
    territory: "forest",
    hunts: true
)

CatFemale extend Feline, Carnivore, Domestic(
    name: "lilly",
    species: "cat",
    sex: "female",
    produces_milk: true,
    territory: "home",
    hunts: false
)

DogMale extend Carnivore, Domestic(
    name: "max",
    species: "dog",
    sex: "male",
    produces_milk: false,
    territory: "yard",
    hunts: false
)

# The source fact owns membership knowledge.  That makes ordinary comparison
# directional, while Relation.compare(mode: "symmetric") keeps both proofs.
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

    tiger_female.relate(
        to: cat_female,
        as: Relationship(name: "shared-carnivore-evidence"),
        degree: 0.78,
        confidence: 0.91
    )

    tied_lineage := lowestCommonAncestor(left: tiger_female, right: cat_female)
    broad_lineage := highestCommonAncestor(left: tiger_female, right: cat_female)
    tiger_cat_lineage := ancestorAnalysis(left: tiger_female, right: cat_female)
    tiger_dog_lineage := commonAncestors(left: tiger_male, right: dog_male)
    habitat_propagation := propagateFact(
        parent: wild,
        child: tiger_female,
        changes: {habitat: "reserve", name: "wild-animal"}
    )

    directed_tiger_cat := Relation.compare(
        left: tiger_female,
        right: cat_female,
        max_depth: 5,
        max_ancestor_depth: 2
    )
    directed_cat_tiger := Relation.compare(
        left: cat_female,
        right: tiger_female,
        max_depth: 5
    )
    symmetric_tiger_cat := Relation.compare(
        left: tiger_female,
        right: cat_female,
        max_depth: 5,
        max_ancestor_depth: 2,
        mode: "symmetric"
    )

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
