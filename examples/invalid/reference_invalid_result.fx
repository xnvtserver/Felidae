Motion(position: 10.0)
Time(seconds: 2.0)

Physics.invalidVelocity(input: Motion, factor: Time) =>
    return 5.0

main() =>
    motion := Motion(position: 10.0)
    motion.references(
        by: Physics::invalidVelocity,
        factor: Time(seconds: 2.0),
        as: Reference(name: "invalid")
    )
    return Fact.references(input: motion)
