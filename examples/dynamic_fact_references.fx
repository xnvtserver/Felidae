# Dynamic references are executable method-to-fact attachments.  They are
# separate from dependencies and relationships and run only via
# Fact.references(...).

Motion(position: 10.0)
Time(seconds: 2.0)

Physics.velocity(input: Motion, factor: Time) =>
    velocity := math:div(left: input.position, right: factor.seconds)
    return ReferenceResult(
        result: Velocity(value: velocity),
        description: "position divided by supplied time"
    )

Physics.waveProjection(input: Motion, factor: Time) =>
    projected := math:mul(left: input.position, right: factor.seconds)
    return ReferenceResult(
        result: WaveProjection(value: projected),
        description: "simple time-based projection"
    )

main() =>
    motion := Motion(position: 10.0)
    defaultTime := Time(seconds: 2.0)
    motion.references(
        by: Physics::velocity,
        factor: defaultTime,
        as: Reference(name: "velocity")
    )
    motion.references(
        by: Physics::waveProjection,
        factor: defaultTime,
        as: Reference(name: "projection")
    )
    defaults := Fact.references(input: motion)
    velocityAtFive := Fact.references(
        input: motion,
        as: Reference(name: "velocity"),
        factor: Time(seconds: 5.0)
    )
    defaultsAgain := Fact.references(input: motion)
    return {
        defaults: defaults,
        velocity_at_five: velocityAtFive,
        defaults_again: defaultsAgain
    }
