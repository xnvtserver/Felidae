import "console".

TenCheck(x: number) =>
    x == 10,
    return (matched: true, message: "x is ten")
else
    return (matched: false, message: "x is not ten").

EchoText() =>
    text := console.input(print: "enter text: "),
    return (text: text).

main() =>
    x := console.inputNumber(print: "enter x: "),
    return (x: x, check: TenCheck(x: x)).
