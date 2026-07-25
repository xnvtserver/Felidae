outputCheck (test_argument:string) => test_argument == "ok".
main(arguments: system.stdin) =>
    status := system.print(value: "Felidae system running!"),
    return outputCheck(status)
