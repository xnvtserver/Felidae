import ("fact.fx", "fact_analysis.fx", "probability").

CandidateNeed(category: string, clearance: string, region: string, skillScore: number, reliability: number) =>
    return ({category: category, clearance: clearance, region: region, skillScore: skillScore, reliability: reliability}).

GenerateSharedFact(name: string, left: any, right: any) =>
    comparison := fact.compareFacts(fact1: left, fact2: right),
    ancestor := fact.commonAncestor(fact1: left, fact2: right),
    return (
        __type: name,
        source: "compareFacts",
        score: comparison.score,
        ancestor: ancestor.ancestor_type,
        generalized: ancestor.generalized_fact,
        differences: comparison.differences
    ).

main() =>
    backend := {__type: "BackendEngineer", __parent: "Employee", name: "Asha", category: "staff", clearance: "internal", region: "south", language: "Felidae", skillScore: 91, reliability: 0.96},
    dataEngineer := {__type: "DataEngineer", __parent: "Employee", name: "Mira", category: "staff", clearance: "internal", region: "south", language: "Felidae", skillScore: 88, reliability: 0.93},
    contractor := {__type: "Contractor", __parent: "ExternalWorker", name: "Dev", category: "external", clearance: "limited", region: "south", language: "Felidae", skillScore: 89, reliability: 0.76},

    sensorA := {__type: "TemperatureSensor", __parent: "Sensor", id: "t-1", category: "iot", site: "line-a", unit: "celsius", temperature: 43.2, vibration: 0.12, load: 31},
    sensorB := {__type: "ThermalProbe", __parent: "Sensor", id: "t-2", category: "iot", site: "line-a", unit: "celsius", temperature: 43.9, vibration: 0.15, load: 33},
    camera := {__type: "VisionCamera", __parent: "OpticalDevice", id: "v-1", category: "iot", site: "line-a", unit: "pixels", temperature: 40.0, vibration: 0.18, load: 28},

    tomatoSauce := {__type: "TomatoSauce", __parent: "Sauce", name: "TomatoBase", category: "sauce", acidity: 8, sweetness: 4, umami: 7, heat: 0, role: "base"},
    pepperSauce := {__type: "PepperSauce", __parent: "Sauce", name: "PepperBase", category: "sauce", acidity: 7, sweetness: 2, umami: 6, heat: 6, role: "base"},
    mangoPuree := {__type: "MangoPuree", __parent: "Fruit", name: "MangoPuree", category: "fruit", acidity: 3, sweetness: 9, umami: 1, heat: 0, role: "sweetness"},

    personnelNeed := CandidateNeed(category: "staff", clearance: "internal", region: "south", skillScore: 90, reliability: 0.95),
    personnelRanking := fact_analysis.nearestFactsWhere(input: personnelNeed, candidates: [backend, dataEngineer, contractor], count: 2, requiredFields: ["category", "clearance", "region"]),

    employeeSibling := fact.compareFacts(fact1: backend, fact2: dataEngineer),
    employeeContractor := fact.compareFacts(fact1: backend, fact2: contractor),
    sensorSibling := fact.compareFacts(fact1: sensorA, fact2: sensorB),
    sensorCamera := fact.compareFacts(fact1: sensorA, fact2: camera),
    sauceSibling := fact.compareFacts(fact1: tomatoSauce, fact2: pepperSauce),
    sauceFruit := fact.compareFacts(fact1: tomatoSauce, fact2: mangoPuree),
    employeeSynset := GenerateSharedFact(name: "EmployeeCapabilitySynset", left: backend, right: dataEngineer),
    sauceSynset := GenerateSharedFact(name: "SauceFlavorSynset", left: tomatoSauce, right: pepperSauce),
    confidence := probability.mean(data: [employeeSibling.score, sensorSibling.score, sauceSibling.score]),

    return (
        employeeSibling: employeeSibling,
        employeeContractor: employeeContractor,
        sensorSibling: sensorSibling,
        sensorCamera: sensorCamera,
        sauceSibling: sauceSibling,
        sauceFruit: sauceFruit,
        personnelRanking: personnelRanking,
        employeeSynset: employeeSynset,
        sauceSynset: sauceSynset,
        scenarioConfidence: confidence
    ).
