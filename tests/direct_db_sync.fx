def main() =>
    def synchronized := db.sync(path: "direct_sync_source.fx").
    (synchronized: synchronized, rows: ImportedSchool.all()).
end
