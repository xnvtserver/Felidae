main() =>
    synchronized := db.sync(path: "direct_sync_source.fx")
    return (synchronized: synchronized, rows: ImportedSchool.all())
