# Core Form/IR workload: no imports, native services, queries, or mixfix.
# It intentionally exercises the language constructs that the source compiler
# must eventually lower into one verified, standalone .fir module.

Person(name: "unknown", age: 0, active: false)
Employee extend Person(name: "unknown", age: 0, active: true, role: "staff")
Engineer extend Employee(name: "unknown", age: 0, active: true, role: "engineer", level: 1)

increment(value: number) =>
    return value + 1

double(value: number) =>
    return value * 2

scoreFor(age: number, level: number) =>
    base := double(value: age)
    return base + level

factorial(value: number) =>
    if value <= 1 then
        return 1
    else
        previous := factorial(value: value - 1)
        return value * previous

makeEngineer(name: string, age: number, level: number) =>
    score := scoreFor(age: age, level: level)
    return Engineer(
        name: name,
        age: age,
        active: true,
        role: "engineer",
        level: level,
        score: score
    )

makeReport(person: Person, sequence: number) =>
    next := increment(value: sequence)
    return {
        next: next,
        tags: ["core", "form", "ir"],
        metrics: {
            sequence: sequence,
            factorial: factorial(value: 5)
        }
    }

main() =>
    ada := makeEngineer(name: "Ada", age: 32, level: 4)
    grace := makeEngineer(name: "Grace", age: 37, level: 5)
    adaReport := makeReport(person: ada, sequence: 1)
    graceReport := makeReport(person: grace, sequence: 2)
    return {
        family: "Person/Employee/Engineer",
        people: [ada, grace],
        reports: [adaReport, graceReport],
        totalScore: scoreFor(age: 32, level: 4) + scoreFor(age: 37, level: 5),
        recursiveCheck: factorial(value: 6)
    }
