class Measurement
    def sensor: string.
    def value: number.
end

def main() =>
    Measurement.insert(values: {
        sensor: "temperature",
        value: "not-a-number"
    }).
end
