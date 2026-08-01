RemoteEmployee(name: "Anu", role: "Student")

remoteRole(name: name, role: role) =>
    RemoteEmployee(name: name, role: role)
    message := str:concat(
        left: str:concat(left: "name: ", right: name),
        right: str:concat(left: ", role: ", right: role)
    )
    return message

main() =>
    return remoteRole(name: "Anu", role: "Student")
