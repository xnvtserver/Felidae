import "ml".

main() =>
    readings := [
        {__type: "SensorReading", machine: "press-a", vibration: 0.2, temperature: 42, load: 30, state: "normal"},
        {__type: "SensorReading", machine: "press-b", vibration: 0.3, temperature: 45, load: 34, state: "normal"},
        {__type: "SensorReading", machine: "press-c", vibration: 0.8, temperature: 82, load: 76, state: "risk"},
        {__type: "SensorReading", machine: "press-d", vibration: 0.9, temperature: 88, load: 81, state: "risk"},
        {__type: "SensorReading", machine: "press-e", vibration: 0.25, temperature: 44, load: 32, state: "normal"}
    ],
    profile := ml.profile_facts(facts: readings, features: ["vibration", "temperature", "load"]),
    clusters := ml.cluster_facts(facts: readings, features: ["vibration", "temperature", "load"], clusters: 2),
    correlation := ml.correlate_facts(facts: readings, left: "temperature", right: "load"),
    stateModel := ml.train_decision_tree(facts: readings, target: "state", features: ["vibration", "temperature", "load"]),
    newReading := ml.predict(model: stateModel, input: {vibration: 0.85, temperature: 84, load: 79}),
    return (
        profile: profile,
        clusters: clusters,
        correlation: correlation,
        stateModel: stateModel,
        newReading: newReading
    )
