# --check-json warns about a name a fact pattern binds and never uses again:
# `whoo` below is almost certainly a typo for `who`, and as written it matches
# every Person. `_` and names starting with `_` mean "ignore this field".
def Person(name: "Ada", age: 36, address: _).
def Person(name: "Bob", age: 20, address: _).

def ageOf(who: string) =>
    def Person(name: who, age: a, address: _).
    (age: a).
end

def ageTypo(who: string) =>
    def Person(name: whoo, age: a, address: _unused).
    (age: a).
end

def main() =>
    (ada: ageOf(who: "Ada"), typo: ageTypo(who: "Ada")).
end
