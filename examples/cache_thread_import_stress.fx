import "lazy_modules/more_people.fx".
import "thread".

ImportedPeople() =>
    names := lambda(Employee, p => p.name),
    return (
        count: count(names),
        names: names
    ).

ImportedRoles() =>
    roles := lambda(Employee, p => p.role),
    return (
        count: count(roles),
        roles: roles
    ).

main() =>
    t1 := thread.createThread(function: "ImportedPeople"),
    t2 := thread.createThread(function: "ImportedRoles"),
    t3 := thread.createThread(function: "ImportedPeople"),
    start1 := thread.start(thread: t1),
    start2 := thread.start(thread: t2),
    start3 := thread.start(thread: t3),
    result1 := thread.result(thread: t1),
    result2 := thread.result(thread: t2),
    result3 := thread.result(thread: t3),
    return (
        start1: start1,
        start2: start2,
        start3: start3,
        result1: result1,
        result2: result2,
        result3: result3
    ).
