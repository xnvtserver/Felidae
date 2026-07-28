Motion(position: 10.0)
Time(seconds: 2.0)

Physics.impureVelocity(input: Motion, factor: Time) =>
    system.print(value: "not allowed during reference evaluation")
    return ReferenceResult(result: Velocity(value: 5.0))

main() =>
    motion := Motion(position: 10.0)
    motion.references(
        by: Physics::impureVelocity,
        factor: Time(seconds: 2.0),
        as: Reference(name: "impure")
    )
    return nil
