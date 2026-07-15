FirstStructure := StructureTest(a: {x: 1, y: 2, z: {w: "hello", v: "world"}}).

Structures := [
    FirstStructure,
    StructureTest(a: {x: 3, y: 4, z: {w: "bonjour", v: "monde"}})
].

StructureExtractionTest(x: number, w: string) =>
    a := FirstStructure,
    x == a.a.x,
    w == a.a.z.w.


main() =>
    system.print(value: "Hello, World!"),
    return (
      StructureExtractionTest(x: 1, w: "hello")
    ).