# `as animals` is a compiler designation for the Animal query root. The
# QueryFacts ISA operation includes Animal plus every registered descendant.

Animal(name: "generic") as animals
Dog extend Animal(name: "fido")
Cat extend Animal(name: "milo")

keep(fact: any) => return fact

main() => return for_each_fact(animals, keep)
