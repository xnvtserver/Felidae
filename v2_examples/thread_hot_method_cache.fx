import "thread".

HotAdd(value: int) =>
    doubled := value + value,
    return (result: doubled).

WorkerOne() =>
    HotAdd(value: 1, result: a),
    HotAdd(value: a, result: b),
    HotAdd(value: b, result: c),
    return (result: c).

WorkerTwo() =>
    HotAdd(value: 2, result: a),
    HotAdd(value: a, result: b),
    HotAdd(value: b, result: c),
    return (result: c).

WorkerThree() =>
    HotAdd(value: 3, result: a),
    HotAdd(value: a, result: b),
    HotAdd(value: b, result: c),
    return (result: c).

WorkerFour() =>
    HotAdd(value: 4, result: a),
    HotAdd(value: a, result: b),
    HotAdd(value: b, result: c),
    return (result: c).

main() =>
    t1 := thread.createThread(function: "WorkerOne"),
    t2 := thread.createThread(function: "WorkerTwo"),
    t3 := thread.createThread(function: "WorkerThree"),
    t4 := thread.createThread(function: "WorkerFour"),
    s1 := thread.start(thread: t1),
    s2 := thread.start(thread: t2),
    s3 := thread.start(thread: t3),
    s4 := thread.start(thread: t4),
    r1 := thread.result(thread: t1),
    r2 := thread.result(thread: t2),
    r3 := thread.result(thread: t3),
    r4 := thread.result(thread: t4),
    return (started1: s1, started2: s2, started3: s3, started4: s4, result1: r1, result2: r2, result3: r3, result4: r4).
