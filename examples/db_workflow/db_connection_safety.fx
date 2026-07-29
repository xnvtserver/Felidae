import ("db", "exception")

LibraryActions.handle(result: any) =>
    if result.error.kind == "DuplicateKey" then
        return {handled: true, kind: result.error.kind, action: "keep-existing-record"}
    else
        return {handled: false, kind: result.error.kind, action: "report-library-error"}

main() =>
    connection := db.create(
        model: "SafetyRecord",
        path: "examples/db_workflow/connection_safety.fx"
    )
    db.deleteOne(connection: connection, conditions: {id: 1})

    inserted := db.insert(
        connection: connection,
        key: "id",
        data: {id: 1, name: "original", status: "created", preserved: "yes"}
    )
    duplicate := db.insert(
        connection: connection,
        key: "id",
        data: {id: 1, name: "duplicate", status: "created", preserved: "no"}
    )
    duplicateCheck := exception.from(value: duplicate.data, error: duplicate.error)
    duplicateResolution := LibraryActions.handle(result: duplicateCheck)
    updated := db.updateOne(
        connection: connection,
        conditions: {id: 1, status: "created"},
        patch: {status: "confirmed"}
    )
    staleUpdate := db.updateOne(
        connection: connection,
        conditions: {id: 1, status: "created"},
        patch: {status: "incorrect"}
    )

    # Reconnecting must not erase the existing database.
    reopened := db.create(
        model: "SafetyRecord",
        path: "examples/db_workflow/connection_safety.fx"
    )
    db.sync(path: "examples/db_workflow/connection_safety.fx")
    storedFacts := lambda(SafetyRecord, item => item.id == 1)
    stored := {
        matched: count(storedFacts),
        modified: 0,
        inserted: 0,
        deleted: 0,
        data: array:get(data: storedFacts, position: 0),
        error: nil
    }

    return (
        inserted: inserted,
        duplicate: duplicate,
        handled_library_exception: duplicateResolution.kind,
        updated: updated,
        stale_update: staleUpdate,
        stored: stored
    )
