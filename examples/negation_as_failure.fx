# `not` is a pure, bound-variable goal.  It succeeds only when the positive
# fact lookup has no proof; it does not create explicit negative evidence.
Bird(name: "sparrow")
Bird(name: "penguin")
Penguin(name: "penguin")

CanFly(name: name) =>
    Bird(name: name)
    not Penguin(name: name)
    return

main() =>
    CanFly(name: "sparrow")
    return "proved by absence of a contrary fact"
