# Relation.compare is interpreter-owned.  Membership and target-family rules
# below are ordinary Felidae methods, so no comparison engine, DLL, or db
# import is needed for ordinary in-memory fact reasoning.

Animal(name: "animal", living: true, breathes: true)
Mammal extend Animal(name: "mammal", living: true, breathes: true, warmBlooded: true)
Dog extend Mammal(name: "fido", living: true, breathes: true, warmBlooded: true, hasHair: true)
BrokenDog extend Mammal(name: "blocked", living: true, breathes: true, warmBlooded: true, hasHair: true)
Vehicle(name: "vehicle", wheels: 4)

Dog.membership(input: Dog, against: Mammal) =>
    return {warmBlooded: input.warmBlooded, hasHair: input.hasHair}

Dog.membership(input: Dog, against: Animal) =>
    return {living: input.living, breathes: input.breathes}

# A comparison result participates in the same path through an ordinary
# membership method.  Unresolved/incomparable results intentionally stop a
# chain by returning nil; other structured outcomes remain usable facts.
Comparison.membership(input: Comparison, against: Fact) =>
    if input.state == "unresolved" then
        return nil
    else
        if input.state == "incomparable" then
            return nil
        else
            return {
                previousState: input.state,
                previousFamily: input.targetFamily,
                previousMembership: input.membership,
                previousEvidence: input.evidence
            }

Animal.compareMembership(context: Fact) =>
    return {
        state: "animal-interpreted",
        judgment: "target-owned",
        relationshipCount: count(context.relationships),
        evidence: context.structuralEvidence
    }

main() =>
    # Constructors are fact-compatible values.  Relation.compare resolves
    # their stable logical identities in the built-in in-memory fact store.
    dog := Dog(name: "fido", living: true, breathes: true, warmBlooded: true, hasHair: true)
    bareDog := Dog(name: "fido")
    mammal := Mammal(name: "mammal", living: true, breathes: true, warmBlooded: true)
    animal := Animal(name: "animal", living: true, breathes: true)
    blocked := BrokenDog(name: "blocked", living: true, breathes: true, warmBlooded: true, hasHair: true)
    vehicle := Vehicle(name: "vehicle", wheels: 4)
    dog.relate(to: mammal, as: Relationship(name: "similar"), degree: 0.90, confidence: 0.96)
    blocked.depends(on: Application(id: "missing-app"))
    mammalResult := Relation.compare(left: dog, right: mammal)
    animalResult := Relation.compare(left: dog, right: animal)
    reversed := Relation.compare(left: animal, right: dog)
    unresolved := Relation.compare(left: blocked, right: mammal)
    chained := Relation.compare(left: animalResult, right: mammal)
    pipelined := Relation.compare(left: dog, right: animal)
        then Relation.compare(left: system.result, right: mammal)
    baseExact := Relation.compare(left: vehicle, right: vehicle)
    resolvedFields := Relation.compare(left: bareDog, right: animal)
    return {
        mammal: mammalResult,
        animal: animalResult,
        reversed: reversed,
        unresolved: unresolved,
        chained: chained,
        pipelined: pipelined,
        base_exact: baseExact,
        resolved_fields: resolvedFields
    }
