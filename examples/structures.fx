TupleTest(value: value) =>
    value == fn:tuple(first:"hello", second:"world", third:"!").

MixedTupleTest(value: value) =>
    value == fn:tuple(first:1, second:2, third:"three", fourth:"4").

ArrayLiteralTest(value: value) =>
    array:get(data: [1, 2, 3, 4], position: 2, access: value).

FirstStructure := StructureTest(a: {x: 1, y: 2, z: {w: "hello", v: "world"}}).

Structures := [
    FirstStructure,
    StructureTest(a: {x: 3, y: 4, z: {w: "bonjour", v: "monde"}})
].

StructureExtractionTest(x: number, w: string) =>
    a := FirstStructure,
    x == a.a.x,
    w == a.a.z.w.

JsonNestedTest(object: object, value: value) =>
    json:parse(data: "{\"x\":1,\"z\":{\"w\":\"hello\",\"items\":[1,2,3]}}", access: object),
    value == object.z.w.
