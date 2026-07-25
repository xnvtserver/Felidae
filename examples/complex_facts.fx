Employee(
    name: "Default",
    place: Place(name: "Default", address: "ABC location"),
    role: Role(name: "Default")
)

radius_ := Radius(value: 5, unit: "cm")

Address(
    name: "Default",
    street: "Default",
    city: "Default",
    state: "Default",
    zip: "00000",
    radius: radius_
)

EmployeePlace(place: place, value: value) =>
    Employee(place: place),
    value == place.address.

AddressRadiusUnit(radius: radius, value: value) =>
    Address(radius: radius),
    value == radius.unit.


main(arguments: system.stdin) =>
    employee_place := EmployeePlace(place: {__type: "Place", name: "Default", address: "ABC location"}, value: "ABC location"),
    address_radius_unit := AddressRadiusUnit(radius: {__type: "Radius", value: 5, unit: "cm"}, value: "cm"),
    system.print(value: employee_place),
    system.print(value: address_radius_unit),
    return (
        employee_place: employee_place,
        address_radius_unit: address_radius_unit
    )