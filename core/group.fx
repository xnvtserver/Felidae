import ("flibrary", "system.flibrary.group")

# Finite group operations use an explicit Cayley table:
# [{left: a, right: b, result: c}, ...]

Group.validate(set: array, table: array, identity: any) =>
    return (system_library_loader(module: "group", function: "validate", args: {set: set, table: table, identity: identity}))

Group.closed(set: array, table: array) =>
    return (system_library_loader(module: "group", function: "closed", args: {set: set, table: table}))

Group.associative(set: array, table: array) =>
    return (system_library_loader(module: "group", function: "associative", args: {set: set, table: table}))

Group.identity(set: array, table: array, identity: any) =>
    return (system_library_loader(module: "group", function: "identity", args: {set: set, table: table, identity: identity}))

Group.inverse(set: array, table: array, identity: any) =>
    return (system_library_loader(module: "group", function: "inverse", args: {set: set, table: table, identity: identity}))
