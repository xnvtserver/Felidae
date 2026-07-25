import "list"
List(name: "fruits")
ListItem(list: "fruits", pos: 0, label: "apple", value: "apple")
ListItem(list: "fruits", pos: 1, label: "banana", value: "banana")
ListItem(list: "fruits", pos: 2, label: "mango", value: "mango")

Run() =>
    first := list.get(list: "fruits", pos: 0)
    named := list.get(list: "fruits", label: "mango")
    popped := list.pop(list: "fruits")
    return (first: first, named: named, popped: popped)


TestEmpty() =>()
TestEmpty1() =>{}
TestMethod1() => 
 system.print(value: "ready ...")
 return


main(arguments: system.stdin) =>
    system.print(value:":print Run()")
    system.print(value: Run())
    TestMethod1()
    return nil
