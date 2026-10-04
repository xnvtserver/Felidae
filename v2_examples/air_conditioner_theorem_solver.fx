# HVAC control with hierarchical observations, explicit unification predicates,
# ordered proof alternatives, and fail-safe numeric truth.

def ClimateObservation(zone: "", temperature: 0, humidity: 0, occupied: 0).
def HotObservation extend ClimateObservation(zone: "", temperature: 0, humidity: 0, occupied: 0).
def ColdObservation extend ClimateObservation(zone: "", temperature: 0, humidity: 0, occupied: 0).
def ComfortableObservation extend ClimateObservation(zone: "", temperature: 0, humidity: 0, occupied: 0).

def HvacPlant(name: "", available: 0, sensor_valid: 0).
def HeatPump extend HvacPlant(name: "", available: 0, sensor_valid: 0).

def observationUnifies(observation: any) =>
    return def isA(
        left: observation,
        right: ClimateObservation(
            zone: "",
            temperature: 0,
            humidity: 0,
            occupied: 0
        )
    ).
end

def proveCooling(observation: any, plant: any) =>
    return (
        observationUnifies(observation: observation) == 1.0
        and observation.temperature > 24.0
        and observation.occupied == 1.0
        and plant.available == 1.0
        and plant.sensor_valid == 1.0
    ).
end

def proveHeating(observation: any, plant: any) =>
    return (
        observationUnifies(observation: observation) == 1.0
        and observation.temperature < 19.0
        and observation.occupied == 1.0
        and plant.available == 1.0
        and plant.sensor_valid == 1.0
    ).
end

def proveVentilation(observation: any, plant: any) =>
    return (
        observationUnifies(observation: observation) == 1.0
        and observation.humidity > 65.0
        and plant.available == 1.0
        and plant.sensor_valid == 1.0
    ).
end

def proveIdle(observation: any, plant: any) =>
    return (
        observationUnifies(observation: observation) == 1.0
        and plant.sensor_valid == 1.0
    ).
end

def chooseIdle(idle_proof: number) =>
    if idle_proof == 1.0 then
        return "idle".
    else
        return "fault_lockout".
    end
end

def chooseVentilation(ventilation_proof: number, idle_proof: number) =>
    if ventilation_proof == 1.0 then
        return "ventilate".
    else
        return chooseIdle(idle_proof: idle_proof).
    end
end

def chooseHeating(heating_proof: number, ventilation_proof: number, idle_proof: number) =>
    if heating_proof == 1.0 then
        return "heat".
    else
        return chooseVentilation(
            ventilation_proof: ventilation_proof,
            idle_proof: idle_proof
        ).
    end
end

def chooseCooling(cooling_proof: number, heating_proof: number, ventilation_proof: number, idle_proof: number) =>
    if cooling_proof == 1.0 then
        return "cool".
    else
        return chooseHeating(
            heating_proof: heating_proof,
            ventilation_proof: ventilation_proof,
            idle_proof: idle_proof
        ).
    end
end

@mixfix(pattern: "{evidence: number} warrants {action: string}")
def warrant(evidence: number, action: string) =>
    if evidence == 1.0 then
        return action.
    else
        return "unproved".
    end
end

def solve(observation: any, plant: any) =>
    def cooling_proof := proveCooling(observation: observation, plant: plant)
    def heating_proof := proveHeating(observation: observation, plant: plant)
    def ventilation_proof := proveVentilation(observation: observation, plant: plant)
    def idle_proof := proveIdle(observation: observation, plant: plant)
    def theorem := cooling_proof warrants "cooling"
    def action := chooseCooling(
        cooling_proof: cooling_proof,
        heating_proof: heating_proof,
        ventilation_proof: ventilation_proof,
        idle_proof: idle_proof
    ).
    return (
        action: action,
        theorem: theorem,
        cooling_proof: cooling_proof,
        heating_proof: heating_proof,
        ventilation_proof: ventilation_proof,
        idle_proof: idle_proof
    ).
end

def main() =>
    def plant := HeatPump(name: "north-wing", available: 1.0, sensor_valid: 1.0)
    def faulty := HeatPump(name: "south-wing", available: 1.0, sensor_valid: 0.0)
    def hot := HotObservation(zone: "office", temperature: 29, humidity: 52, occupied: 1.0)
    def humid_empty := HotObservation(zone: "store", temperature: 27, humidity: 72, occupied: 0.0)
    return (
        occupied_hot: solve(observation: hot, plant: plant),
        empty_humid: solve(observation: humid_empty, plant: plant),
        invalid_sensor: solve(observation: hot, plant: faulty)
    ).
end
