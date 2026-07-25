import "thread"

Person(name: "Alice", role: "Engineer")
Person(name: "Bob", role: "Manager")
Person(name: "Carol", role: "Engineer")

EngineerNames() =>
    names := lambda(Person, p => SelectEngineer(input: p))
    return (
        count: count(names),
        names: names
    )

SelectEngineer(input: Person) =>
    where input.role == "Engineer"
    return (input.name)

CountPeople() =>
    people := lambda(Person, p => p.name)
    return (
        count: count(people)
    )

main() =>
    t1 := thread.createThread(function: "EngineerNames")
    t2 := thread.createThread(function: "CountPeople")
    start1 := thread.start(thread: t1)
    start2 := thread.start(thread: t2)
    result1 := thread.result(thread: t1)
    result2 := thread.result(thread: t2)
    status1 := thread.status(thread: t1)
    status2 := thread.status(thread: t2)
    return (
        start1: start1,
        start2: start2,
        status1: status1,
        status2: status2,
        result1: result1,
        result2: result2
    )
