# Fact-file persistence API.
#
# Felidae's interpreter is the default in-memory OLAP fact store: normal
# unification, hierarchy traversal, membership, reasoning, and fact queries
# do not import this file. db.fx is intentionally limited to explicit .fx
# source-file connection, mutation, save, and reload workflows.

import ("flibrary", "file", "system.flibrary.db")

db.sync(path: string) => ()

db.read(path: string) =>
    return file.readFile(path: path)

db.set(model: string, path: string) =>
    return db.connect(model: model, path: path)

db.connect(model: string, path: string) =>
    cleanModel := str.trim(data: model)
    opened := system_library_loader(
        module: "db",
        function: "connect",
        args: {rows: [], path: path, models: [cleanModel]}
    )
    return {model: cleanModel, path: path}

db.create(model: string) =>
    cleanModel := str.trim(data: model)
    path := str.concat(left: cleanModel, right: ".fx")
    return db.connect(model: cleanModel, path: path)

db.create(model: string, path: string) =>
    return db.connect(model: model, path: path)

db.create(models: array, path: string) =>
    return (system_library_loader(
        module: "db",
        function: "connect",
        args: {rows: [], path: path, models: models}
    ))

db.create(path: string, type: string, rows: array) =>
    schema := system_library_loader(module: "db", function: "schema", args: {rows: [], models: [type]})
    file.writeFile(path: path, data: schema, mode: "write")
    return db.save(path: path, type: type, rows: rows)

db.write(path: string, type: string, rows: array) =>
    return db.save(path: path, type: type, rows: rows)

db.insert(connection: any, data: any) =>
    result := system_library_loader(
        module: "db",
        function: "insertOneFile",
        args: {rows: [], path: connection.path, type: connection.model, key: "id", data: data}
    )
    return db.operationResult(result: result)

db.insert(connection: any, data: any, key: string) =>
    result := system_library_loader(
        module: "db",
        function: "insertOneFile",
        args: {rows: [], path: connection.path, type: connection.model, key: key, data: data}
    )
    return db.operationResult(result: result)

db.updateOne(connection: any, conditions: any, patch: any) =>
    result := system_library_loader(
        module: "db",
        function: "updateOneFile",
        args: {rows: [], path: connection.path, type: connection.model, conditions: conditions, patch: patch}
    )
    return db.operationResult(result: result)

db.deleteOne(connection: any, conditions: any) =>
    result := system_library_loader(
        module: "db",
        function: "deleteOneFile",
        args: {rows: [], path: connection.path, type: connection.model, conditions: conditions}
    )
    return db.operationResult(result: result)

db.operationResult(result: any) =>
    if result.problem != nil then
        return {
            matched: result.matched,
            modified: result.modified,
            inserted: result.inserted,
            deleted: result.deleted,
            data: result.data,
            error: {
                __type: "Exception",
                kind: result.problem,
                message: result.problem,
                source: "db"
            }
        }
    else
        return {
            matched: result.matched,
            modified: result.modified,
            inserted: result.inserted,
            deleted: result.deleted,
            data: result.data,
            error: nil
        }

db.save(path: string, type: string, rows: array) =>
    return system_library_loader(
        module: "db",
        function: "saveModelFile",
        args: {rows: rows, path: path, type: type}
    )

db.save(database: any, rows: array) =>
    return db.save(path: database.path, type: database.model, rows: rows)
