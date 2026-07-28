# Fact database API.
#
# Query operations read indexed facts loaded by imports. Mutation operations
# return new immutable row arrays; save persists an array as an importable .fx
# fact database. This keeps updates explicit and avoids hidden global mutation.

import ("flibrary", "file", "system.flibrary.db")

db.all(type: string) => ()
db.find(type: string, field: string, equals: any) => ()
db.count(type: string) => ()
db.first(type: string, field: string, equals: any) => ()
db.types() => ()
db.fields(type: string) => ()
db.exists(type: string, field: string, equals: any) => ()
db.select(type: string) => ()
db.select(type: string, field: string, equals: any) => ()
db.materialize(selection: any) => ()
db.release(selection: any) => ()
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

db.add(rows: array, row: any) =>
    return (system_library_loader(module: "db", function: "add", args: {rows: rows, row: row}))

db.insert(path: string, type: string, rows: array, row: any) =>
    updated := db.add(rows: rows, row: row)
    db.save(path: path, type: type, rows: updated)
    return updated

db.insert(database: any, rows: array, row: any) =>
    return db.insert(path: database.path, type: database.model, rows: rows, row: row)

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

db.findMany(connection: any, conditions: any) =>
    result := system_library_loader(
        module: "db",
        function: "findManyFile",
        args: {rows: [], path: connection.path, type: connection.model, conditions: conditions}
    )
    return db.operationResult(result: result)

db.findOne(connection: any, conditions: any) =>
    result := system_library_loader(
        module: "db",
        function: "findOneFile",
        args: {rows: [], path: connection.path, type: connection.model, conditions: conditions}
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

db.select(rows: array, field: string, equals: any) =>
    return (system_library_loader(module: "db", function: "find", args: {rows: rows, field: field, equals: equals}))

db.sort(rows: array, field: string, direction: string) =>
    return (system_library_loader(module: "db", function: "sort", args: {rows: rows, field: field, direction: direction}))

db.search(rows: array, field: string, query: string) =>
    return (system_library_loader(module: "db", function: "search", args: {rows: rows, field: field, query: query}))

db.distinct(rows: array, field: string) =>
    return (system_library_loader(module: "db", function: "distinct", args: {rows: rows, field: field}))

db.paginate(rows: array, offset: number, limit: number) =>
    return (system_library_loader(module: "db", function: "paginate", args: {rows: rows, offset: offset, limit: limit}))

db.aggregate(rows: array, field: string, operation: string) =>
    return (system_library_loader(module: "db", function: "aggregate", args: {rows: rows, field: field, operation: operation}))

db.findOne(type: string, field: string, equals: any) =>
    return db.first(type: type, field: field, equals: equals)

db.update(rows: array, field: string, equals: any, patch: any) =>
    return (system_library_loader(module: "db", function: "update", args: {rows: rows, field: field, equals: equals, patch: patch}))

db.updateFile(path: string, type: string, rows: array, field: string, equals: any, patch: any) =>
    updated := db.update(rows: rows, field: field, equals: equals, patch: patch)
    db.save(path: path, type: type, rows: updated)
    return updated

db.updateFile(database: any, rows: array, field: string, equals: any, patch: any) =>
    return db.updateFile(path: database.path, type: database.model, rows: rows, field: field, equals: equals, patch: patch)

db.replace(rows: array, field: string, equals: any, replacement: any) =>
    withoutMatches := db.delete(rows: rows, field: field, equals: equals)
    return db.add(rows: withoutMatches, row: replacement)

db.upsert(rows: array, field: string, equals: any, row: any) =>
    withoutMatches := db.delete(rows: rows, field: field, equals: equals)
    return db.add(rows: withoutMatches, row: row)

db.upsertFile(path: string, type: string, rows: array, field: string, equals: any, row: any) =>
    updated := db.upsert(rows: rows, field: field, equals: equals, row: row)
    db.save(path: path, type: type, rows: updated)
    return updated

db.upsertFile(database: any, rows: array, field: string, equals: any, row: any) =>
    return db.upsertFile(path: database.path, type: database.model, rows: rows, field: field, equals: equals, row: row)

db.delete(rows: array, field: string, equals: any) =>
    return (system_library_loader(module: "db", function: "remove", args: {rows: rows, field: field, equals: equals}))

db.deleteFile(path: string, type: string, rows: array, field: string, equals: any) =>
    updated := db.delete(rows: rows, field: field, equals: equals)
    db.save(path: path, type: type, rows: updated)
    return updated

db.deleteFile(database: any, rows: array, field: string, equals: any) =>
    return db.deleteFile(path: database.path, type: database.model, rows: rows, field: field, equals: equals)

# Cascade behavior is explicit: callers name both relationship fields. Felidae
# does not create or enforce a foreign key.
db.deleteCascade(parents: array, parentField: string, equals: any, dependents: array, dependentField: string) =>
    return (system_library_loader(module: "db", function: "deleteCascade", args: {rows: parents, parentField: parentField, equals: equals, dependents: dependents, dependentField: dependentField}))

db.deleteCascadeFile(parentPath: string, parentType: string, parents: array, parentField: string, equals: any, dependentPath: string, dependentType: string, dependents: array, dependentField: string) =>
    updated := db.deleteCascade(parents: parents, parentField: parentField, equals: equals, dependents: dependents, dependentField: dependentField)
    db.save(path: parentPath, type: parentType, rows: updated.parents)
    db.save(path: dependentPath, type: dependentType, rows: updated.dependents)
    return updated

db.save(path: string, type: string, rows: array) =>
    source := file.readFile(path: path)
    facts := system_library_loader(module: "db", function: "merge", args: {rows: rows, type: type, source: source})
    return file.writeFile(path: path, data: facts, mode: "write")

db.save(database: any, rows: array) =>
    return db.save(path: database.path, type: database.model, rows: rows)
