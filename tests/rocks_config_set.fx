import "db".

class DatabaseProbe
    key(id).
    id: string.
end

DatabaseProbe(id: "configuration-write").

def main() =>
    configured := db.configure(options: {
        max_background_jobs: 3,
        bytes_per_sync: 1048576
    }).
    statistics := db.stats().
    return (
        max_background_jobs: configured.max_background_jobs,
        bytes_per_sync: configured.bytes_per_sync,
        fact_writes: statistics.fact_writes
    ).
end
