import "db".

def main() =>
    def configured := db.config().
    (
        max_background_jobs: configured.max_background_jobs,
        bytes_per_sync: configured.bytes_per_sync
    ).
end
