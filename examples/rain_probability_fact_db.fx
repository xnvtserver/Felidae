import ("db" "probability").

Rain(
    month: "August",
    day: {id: "monday", chance: 5.2},
    day: {id: "tuesday", chance: 0.3},
    day: {id: "wednesday", chance: 12.5},
    day: {id: "thursday", chance: 30.0}
).

Rain(
    month: "September",
    day: {id: "monday", chance: 19.0},
    day: {id: "tuesday", chance: 24.0},
    day: {id: "wednesday", chance: 11.0}
).

RainMonth(month: string) =>
    record := db.first(type: "Rain", field: "month", equals: month),
    chances := lambda(record.day, day => day.chance),
    normalized := probability.normalize(data: chances),
    averageChance := probability.mean(data: chances),
    averageProbability := averageChance / 100,
    atMostOneRainyDay := probability.binomialCdf(
        trials: count(chances),
        successes: 1,
        p: averageProbability
    ),
    return (
        month: month,
        days: record.day,
        chances: chances,
        normalized_distribution: normalized,
        average_chance_percent: averageChance,
        at_most_one_rainy_day_probability: atMostOneRainyDay
    ).

main() =>
    august := RainMonth(month: "August"),
    september := RainMonth(month: "September"),
    return (
        august: august,
        september: september
    ).
