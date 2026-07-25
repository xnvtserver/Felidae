import ("fact.fx", "fact_analysis.fx")

main() =>
    sameShapePlant := {__type: "PlantSensor", __parent: "Sensor", category: "device", site: "lab", unit: "celsius", reading: 25.0, health: 0.9},
    sameShapeTool := {__type: "Thermometer", __parent: "Tool", category: "device", site: "lab", unit: "celsius", reading: 25.2, health: 0.88},
    sameAncestorDifferentProps := {__type: "PressureSensor", __parent: "Sensor", category: "device", site: "field", unit: "bar", reading: 8.4, health: 0.5},
    missingFields := {__type: "MinimalSensor", __parent: "Sensor", category: "device", reading: 25.1},
    requiredNeed := {category: "device", site: "lab", unit: "celsius", reading: 25.0, health: 0.9},

    propertyNoAncestor := fact.compareFacts(fact1: sameShapePlant, fact2: sameShapeTool),
    ancestorPoorProperties := fact.compareFacts(fact1: sameShapePlant, fact2: sameAncestorDifferentProps),
    missingFieldComparison := fact.compareFacts(fact1: sameShapePlant, fact2: missingFields),
    exactPropertyComparison := fact.compareProperties(fact1: sameShapePlant, fact2: sameShapeTool),
    constrained := fact_analysis.nearestFactsWhere(input: requiredNeed, candidates: [sameShapePlant, sameShapeTool, sameAncestorDifferentProps, missingFields], count: 4, requiredFields: ["category", "site", "unit"]),

    return (
        propertyNoAncestor: propertyNoAncestor,
        ancestorPoorProperties: ancestorPoorProperties,
        missingFieldComparison: missingFieldComparison,
        exactPropertyComparison: exactPropertyComparison,
        constrained: constrained
    )
