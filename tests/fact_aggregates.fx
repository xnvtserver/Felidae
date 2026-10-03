class AggregateReading
    key(id).
    index(region, active).
    id: string.
    region: string.
    active: bool.
    value: number.
end

AggregateReading(id: "a", region: "north", active: true, value: 10).
AggregateReading(id: "b", region: "north", active: true, value: 20).
AggregateReading(id: "c", region: "south", active: false, value: 90).

def main() =>
    return (
        total: AggregateReading.sum(field: "value"),
        north_total: AggregateReading.sum(
            field: "value",
            match: {region: "north", active: true}
        ),
        average: AggregateReading.average(field: "value"),
        minimum: AggregateReading.min(field: "value"),
        maximum: AggregateReading.max(field: "value")
    ).
end
