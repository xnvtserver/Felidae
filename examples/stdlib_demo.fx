import "../stdlib/*".
import "lazy_modules".

PairDemo(value: value) =>
    value == fn:pair(first: "hello", last: fn:pair(first: "world", last: "!")).

ArraySecond(array: array1, value: value) =>
    array1 := fn:array(data: ["zero", "one", "two"]),
    array:get(data: array1, position: 1, access: value).

ArrayLiteralSecond(value: value) =>
    array:get(data: ["zero", "one", "two"], position: 1, access:value).

JsonName(value: value) =>
    object := json:object(field: fn:pair(first: "name", last: "Alice"), field: fn:pair(first: "score", last: 2.0)),
    json:get(data: object, key: "name", access: value).

JsonParsedScore(object: object, value: value) =>
    json:parse(data: "{\"name\":\"Alice\",\"score\":2.0}", access: object),
    json:get(data: object, key: "score", access: value).

StringSize(size: size) =>
    str:len(data: "hello",equals: size).

MathTotal(total: total) =>
    math:add(lhs: 2.0, rhs: 3.0, result: total).
