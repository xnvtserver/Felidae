class Measurement
    sensor: string
    value: number
end

main() =>
    return Measurement.insert(values: {
        sensor: "temperature",
        value: "not-a-number"
    })
end
