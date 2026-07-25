import "thread"
import "smoke"

Worker() =>
    echoed := smoke.echo(value: "thread native ok")
    return (
        status: echoed
    )

main() =>
    worker := thread.createThread(function: "Worker")
    started := thread.start(thread: worker)
    result := thread.result(thread: worker)
    return (
        started: started,
        result: result
    )
