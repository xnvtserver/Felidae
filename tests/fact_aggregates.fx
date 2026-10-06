class AggregateReading
    key(id).
    index(region, active).
    def id: string.
    def region: string.
    def active: bool.
    def value: number.
end

def AggregateReading(id: "a", region: "north", active: true, value: 10).
def AggregateReading(id: "b", region: "north", active: true, value: 20).
def AggregateReading(id: "c", region: "south", active: false, value: 90).

def main() =>
    (
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
