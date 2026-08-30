# Concrete parent-type queries include every registered descendant.

Animal(name: "generic")
Dog extend Animal(name: "fido")
Cat extend Animal(name: "milo")

main() => return Animal.all()
