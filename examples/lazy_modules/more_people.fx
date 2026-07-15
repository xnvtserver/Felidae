Employee(name: "Eve", role: "Engineer", office: "NYC").
Employee(name: "Frank", role: "Manager", office: "NYC").
Employee(name: "Grace", role: "Engineer", office: "SEA").
Employee(name: "Heidi", role: "Engineer", office: "NYC").
Employee(name: "Ivan", role: "Engineer", office: "NYC").
Employee(name: "Judy", role: "Manager", office: "NYC").
Employee(name: "Kevin", role: "Engineer", office: "NYC").
Employee(name: "Laura", role: "Manager", office: "SEA").
Employee(name: "Mike", role: "Engineer", office: "NYC").
Employee(name: "Nick", role: "Engineer", office: "NYC").
Employee(name: "Olivia", role: "Engineer", office: "NYC").
Employee(name: "Paul", role: "Manager", office: "SEA").

RemoteEngineer(name: name) =>
    output:= Employee(name: name, role: "Engineer", office: "NYC"),
    return output.

main() =>
    remoteEngineer := RemoteEngineer(name: "Grace"),
    system.print(value: remoteEngineer),
    return (
        remoteEngineer: remoteEngineer
    ).