import "db".

def main() =>
    configured := db.config(),
    return (
        max_background_jobs: configured.max_background_jobs,
        bytes_per_sync: configured.bytes_per_sync
    )
end
