import "db"

libraryActions.handle(exception: {
    __type: "Exception",
    kind: "DuplicateKey",
    message: message,
    source: "db"
}) =>
    return true

HandleLibraryException(exception: any) =>
    throw(exception: exception, target: libraryActions::handle)
    return exception.kind

main() =>
    connection := db.create(
        model: "SafetyRecord",
        path: "examples/db_workflow/connection_safety.fx"
    )
    db.deleteOne(connection: connection, conditions: {id: 1})

    inserted := db.insert(
        connection: connection,
        data: {id: 1, name: "original", status: "created", preserved: "yes"}
    )
    duplicate := db.insert(
        connection: connection,
        data: {id: 1, name: "duplicate", status: "created", preserved: "no"}
    )
    handledLibraryException := HandleLibraryException(exception: duplicate.error)
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
    stored := db.findOne(connection: reopened, conditions: {id: 1})

    return (
        inserted: inserted,
        duplicate: duplicate,
        handled_library_exception: handledLibraryException,
        updated: updated,
        stale_update: staleUpdate,
        stored: stored
    )
