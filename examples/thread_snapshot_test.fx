import "thread"

Worker() =>
    return (
        status: "done"
    )

main() =>
    t1 := thread.createThread(function: "Worker")
    started := thread.start(thread: t1)
    result := thread.result(thread: t1)
    status := thread.status(thread: t1)
    return (
        started: started,
        status: status,
        result: result
    )
