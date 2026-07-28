import ("fact.fx")

LivingThing(domain: "being")
MortalBeing extend LivingThing(mortality: true, lifeCycle: "finite", canDie: true)
Human extend MortalBeing(species: "homo sapiens", mortality: true, reasoning: "high")
Man extend Human(species: "homo sapiens", mortality: true, adult: true)
Socrates extend Man(name: "Socrates", philosopher: true, city: "Athens")
Ram extend Man(name: "Ram", philosopher: false, city: "Ayodhya")
Statue(name: "StoneStatue", species: "none", mortality: false, adult: false)

ProveMortalByAncestry(entity: any, mortalKind: any, corpus: array) =>
    path := fact.shortestPath(fact1: entity, fact2: mortalKind, facts: corpus)
    path.reachable == "true"
    ancestors := fact.ancestorClosure(fact: entity, facts: corpus)
    return (
        subject: entity.name,
        mortal: true,
        rule: "ancestor_path_to_mortal_kind",
        path: path,
        ancestors: ancestors,
        inheritedMortality: entity.mortality,
        inheritedSpecies: entity.species
    )
else
    return (
        subject: entity.name,
        mortal: false,
        rule: "no_mortal_ancestor_path"
    )

ProveMortalByFactComparison(entity: any, exemplar: any, mortalKind: any, corpus: array) =>
    exemplarProof := ProveMortalByAncestry(entity: exemplar, mortalKind: mortalKind, corpus: corpus)
    exemplarProof.mortal == true
    comparison := fact.compareFacts(fact1: entity, fact2: exemplar, facts: corpus)
    graphSimilarity := fact.wuPalmerSimilarity(fact1: entity, fact2: exemplar, facts: corpus)
    comparison.score >= 0.75
    graphSimilarity.score >= 0.75
    entity.mortality == exemplar.mortality
    return (
        subject: entity.name,
        mortal: true,
        rule: "mortal_by_inherited_fact_similarity_to_proven_mortal",
        exemplar: exemplar.name,
        exemplarProof: exemplarProof,
        comparison: comparison,
        graphSimilarity: graphSimilarity,
        inheritedMortality: entity.mortality,
        inheritedSpecies: entity.species
    )
else
    return (
        subject: entity.name,
        mortal: false,
        rule: "insufficient_fact_similarity_to_proven_mortal"
    )

main() =>
    livingThings := lambda(LivingThing, item => item.domain == "being")
    mortalBeings := lambda(MortalBeing, item => item.lifeCycle == "finite")
    humans := lambda(Human, item => item.reasoning == "high")
    men := lambda(Man, item => item.adult == true)
    socratesFacts := lambda(Socrates, item => item.name == "Socrates")
    ramFacts := lambda(Ram, item => item.name == "Ram")
    statues := lambda(Statue, item => item.name == "StoneStatue")
    livingThing := array:get(data: livingThings, position: 0)
    mortalBeing := array:get(data: mortalBeings, position: 0)
    human := array:get(data: humans, position: 0)
    man := array:get(data: men, position: 0)
    socrates := array:get(data: socratesFacts, position: 0)
    ram := array:get(data: ramFacts, position: 0)
    statue := array:get(data: statues, position: 0)
    corpus := [livingThing, mortalBeing, human, man, socrates, ram, statue]

    socratesProof := ProveMortalByAncestry(entity: socrates, mortalKind: mortalBeing, corpus: corpus)
    ramProof := ProveMortalByFactComparison(entity: ram, exemplar: socrates, mortalKind: mortalBeing, corpus: corpus)
    statueComparison := fact.compareFacts(fact1: statue, fact2: socrates, facts: corpus)
    statueGraphSimilarity := fact.wuPalmerSimilarity(fact1: statue, fact2: socrates, facts: corpus)
    manToHuman := fact.directRelation(parent: human, child: man)
    socratesToMan := fact.directRelation(parent: man, child: socrates)
    ramToMan := fact.directRelation(parent: man, child: ram)
    socratesRamComparison := fact.compareFacts(fact1: socrates, fact2: ram, facts: corpus)
    socratesRamLca := fact.commonAncestor(fact1: socrates, fact2: ram, facts: corpus)
    socratesRamPath := fact.shortestPath(fact1: socrates, fact2: ram, facts: corpus)
    inheritedFields := {
        socratesMortality: socrates.mortality,
        ramMortality: ram.mortality,
        socratesSpecies: socrates.species,
        ramSpecies: ram.species
    }

    return (
        socratesProof: socratesProof,
        ramProof: ramProof,
        statueComparison: statueComparison,
        statueGraphSimilarity: statueGraphSimilarity,
        manToHuman: manToHuman,
        socratesToMan: socratesToMan,
        ramToMan: ramToMan,
        inheritedFields: inheritedFields,
        socratesRamComparison: socratesRamComparison,
        socratesRamLowestCommonAncestor: socratesRamLca,
        socratesRamPath: socratesRamPath
    )
