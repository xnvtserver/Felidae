# HVAC control with hierarchical observations, explicit unification predicates,
# ordered proof alternatives, and fail-safe numeric truth.

ClimateObservation(zone: "", temperature: 0, humidity: 0, occupied: 0)
HotObservation extend ClimateObservation(zone: "", temperature: 0, humidity: 0, occupied: 0)
ColdObservation extend ClimateObservation(zone: "", temperature: 0, humidity: 0, occupied: 0)
ComfortableObservation extend ClimateObservation(zone: "", temperature: 0, humidity: 0, occupied: 0)

HvacPlant(name: "", available: 0, sensor_valid: 0)
HeatPump extend HvacPlant(name: "", available: 0, sensor_valid: 0)

observationUnifies(observation: any) =>
    return isA(
        left: observation,
        right: ClimateObservation(
            zone: "",
            temperature: 0,
            humidity: 0,
            occupied: 0
        )
    )

proveCooling(observation: any, plant: any) =>
    return (
        observationUnifies(observation: observation) == 1.0
        and observation.temperature > 24.0
        and observation.occupied == 1.0
        and plant.available == 1.0
        and plant.sensor_valid == 1.0
    )

proveHeating(observation: any, plant: any) =>
    return (
        observationUnifies(observation: observation) == 1.0
        and observation.temperature < 19.0
        and observation.occupied == 1.0
        and plant.available == 1.0
        and plant.sensor_valid == 1.0
    )

proveVentilation(observation: any, plant: any) =>
    return (
        observationUnifies(observation: observation) == 1.0
        and observation.humidity > 65.0
        and plant.available == 1.0
        and plant.sensor_valid == 1.0
    )

proveIdle(observation: any, plant: any) =>
    return (
        observationUnifies(observation: observation) == 1.0
        and plant.sensor_valid == 1.0
    )

chooseIdle(idle_proof: number) =>
    if idle_proof == 1.0 then
        return "idle"
    else
        return "fault_lockout"

chooseVentilation(ventilation_proof: number, idle_proof: number) =>
    if ventilation_proof == 1.0 then
        return "ventilate"
    else
        return chooseIdle(idle_proof: idle_proof)

chooseHeating(heating_proof: number, ventilation_proof: number, idle_proof: number) =>
    if heating_proof == 1.0 then
        return "heat"
    else
        return chooseVentilation(
            ventilation_proof: ventilation_proof,
            idle_proof: idle_proof
        )

chooseCooling(cooling_proof: number, heating_proof: number, ventilation_proof: number, idle_proof: number) =>
    if cooling_proof == 1.0 then
        return "cool"
    else
        return chooseHeating(
            heating_proof: heating_proof,
            ventilation_proof: ventilation_proof,
            idle_proof: idle_proof
        )

@mixfix(pattern: "{evidence: number} warrants {action: string}")
warrant(evidence: number, action: string) =>
    if evidence == 1.0 then
        return action
    else
        return "unproved"

solve(observation: any, plant: any) =>
    cooling_proof := proveCooling(observation: observation, plant: plant)
    heating_proof := proveHeating(observation: observation, plant: plant)
    ventilation_proof := proveVentilation(observation: observation, plant: plant)
    idle_proof := proveIdle(observation: observation, plant: plant)
    theorem := cooling_proof warrants "cooling"
    action := chooseCooling(
        cooling_proof: cooling_proof,
        heating_proof: heating_proof,
        ventilation_proof: ventilation_proof,
        idle_proof: idle_proof
    )
    return (
        action: action,
        theorem: theorem,
        cooling_proof: cooling_proof,
        heating_proof: heating_proof,
        ventilation_proof: ventilation_proof,
        idle_proof: idle_proof
    )

main() =>
    plant := HeatPump(name: "north-wing", available: 1.0, sensor_valid: 1.0)
    faulty := HeatPump(name: "south-wing", available: 1.0, sensor_valid: 0.0)
    hot := HotObservation(zone: "office", temperature: 29, humidity: 52, occupied: 1.0)
    humid_empty := HotObservation(zone: "store", temperature: 27, humidity: 72, occupied: 0.0)
    return (
        occupied_hot: solve(observation: hot, plant: plant),
        empty_humid: solve(observation: humid_empty, plant: plant),
        invalid_sensor: solve(observation: hot, plant: faulty)
    )
