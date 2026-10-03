# A thread runs on an interpreter snapshot that shares the parent's RocksDB
# store, so it sees the stored facts instead of an empty database.
import "thread".

Metric(id: "m1", value: 1).
Metric(id: "m2", value: 2).

def worker() =>
    return Metric.count().
end

def main() =>
    t := thread.createThread(function: "worker").
    thread.start(thread: t).
    return thread.result(thread: t).
end
