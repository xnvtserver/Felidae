import ("array", "ml")

# This example intentionally includes negative numeric literals in facts and arrays.
# It is a lightweight neural-network boundary test for Felidae numeric parsing,
# lambda mapping, native ml.relu, and ml.dot.

InputSample(
    name: "sample-a",
    values: [
        1.0, 3.0, 3.0, 2.0, 0.0,
        0.21, 0.223, 0.221, 0.10, 0.0,
        1.0, 2.0343, 200.0, 12.0, 0.0,
        0.345, 0.56, 0.0, 4.0, 9.0,
        1.0, 2.0343, 200.0, 12.0, 0.0,
        0.345, 0.214, 0.0, 4.0, 9.0,
        1.0, 2.0343, -200.0, 12.0, 0.0,
        0.0, 0.22, 0.0, 4.0, 9.0,
        1.0, 2.0343, 200.0, 12.0, 0.0,
        0.345, 8.123, 0.0, 4.0, 9.0,
        1.0, 2.0343, 200.0, 12.0, 0.0,
        0.345, 0.0, 0.0, 4.0, 9.0,
        1.0, 2.0343, 200.0, 12.0, 0.0,
        0.29, 0.343, 0.0, 4.0, 9.0,
        1.0, 2.0343, 200.0, 12.0, 0.0,
        0.82, 6.343, 0.0, 4.0, 9.0
    ]
)

InputNeuron(
    name: "input-1",
    weights: [
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
    ],
    bias: 3.2
)

InputNeuron(
    name: "input-2",
    weights: [
        0.10, 0.0, 2.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 4.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 5.0, 0.0, 0.0, 6.0, 0.0, 0.0, 8.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, -2.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.30, 0.0, 0.0
    ],
    bias: -3.2
)

HiddenNeuron(name: "hidden-1", weights: [2.0, 1.0], bias: 2.0)
HiddenNeuron(name: "hidden-2", weights: [-1.0, 0.5], bias: 1.0)

NeuronOutput(neuron: any, input: any) =>
    dot := ml.dot(left: neuron.weights, right: input.values)
    bias := neuron.bias
    raw := dot + bias
    return (name: neuron.name, raw: raw, activated: ml.relu(value: raw))

HiddenOutput(neuron: any, values: array) =>
    dot := ml.dot(left: neuron.weights, right: values)
    bias := neuron.bias
    raw := dot + bias
    return (name: neuron.name, raw: raw, activated: ml.relu(value: raw))

Network(input: any) =>
    inputLayer := lambda(InputNeuron, neuron => NeuronOutput(neuron: neuron, input: input))
    activatedInput := lambda(inputLayer, item => item.activated)
    hiddenLayer := lambda(HiddenNeuron, neuron => HiddenOutput(neuron: neuron, values: activatedInput))
    finalOutput := lambda(hiddenLayer, item => ml.relu(value: item.raw))
    return (
        input: input.name,
        inputLayer: inputLayer,
        activatedInput: activatedInput,
        hiddenLayer: hiddenLayer,
        finalOutput: finalOutput,
        negativeLiteral: -200.0
    )

main() =>
    results := lambda(InputSample, sample => Network(input: sample))
    return (results: results)
