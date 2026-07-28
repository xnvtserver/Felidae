Motion(position: 10.0)
Time(seconds: 2.0)

Physics.velocity(input: Motion, factor: Time) =>
    return ReferenceResult(result: Velocity(value: 5.0))

main() =>
    Motion(position: 10.0).references(
        by: Physics::velocity,
        factor: Time(seconds: 2.0),
        as: Reference(name: "velocity")
    )
    return nil
