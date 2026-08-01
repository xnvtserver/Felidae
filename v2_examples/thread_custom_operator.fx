import "thread"

@overload(
    operator: threadCombine,
    pattern: "{left} threadCombine {right}",
    captures: {left: number, right: number},
    result: number,
    precedence: additive,
    associativity: left,
    visibility: private
)
combineInWorker() =>
    return left + right

operatorWorker() =>
    return 20 threadCombine 22

main() =>
    worker := thread.createThread(function: "operatorWorker")
    started := thread.start(thread: worker)
    result := thread.result(thread: worker)
    return (started: started, result: result)
