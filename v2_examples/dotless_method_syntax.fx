# Facts end at the newline; no terminating dot is used.
Person(name: "Ada")
Person(name: "Grace")

# Decimal dots remain part of numeric values.
pi := 3.01

# Empty method declarations require neither return nor a terminating dot.
EmptyParen() => ()
EmptyBrace() => {}

# Non-empty multiline methods must contain a return.
Greeting(name: string) =>
    return (message: name)

# A bare return is valid and ends the method.
NoResult() => return

# Member-access dots remain part of expressions.
main(arguments: system.stdin) =>
    greeting := Greeting(name: "Felidae")
    return (message: greeting.message, pi: pi)
