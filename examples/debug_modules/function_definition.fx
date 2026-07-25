RemoteEmployee(name: "Anu", role: "Student")

RemoteRole(name: name, role: role) =>
    RemoteEmployee(name: name, role: role)
    message := str:concat(
        left: str:concat(left: "RemoteRole called with name: ", right: name),
        right: str:concat(left: ", role: ", right: role)
    )
    system.print(value: message)
    return (
        name: name,
        role: role
    )
