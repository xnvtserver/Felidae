# Felidae standard library marker.
#
# Core stdlib operations are implemented as native predicates/terms:
#
# Terms:
#   fn:pair(first: left, last: right)
#   fn:tuple(value: value1, value: value2, ...)
#   fn:array(data: [value1, value2, ...])
#   json:object(field: fn:pair(first: "key", last: value), ...)
#   {key: value}
#   [value1, value2, ...]
#
# Naming:
#   package.Call(...) is for top-level package/module calls.
#   value.field is for map/object field access.
#   Predicate(name: value) is for named arguments.
#   "," between goals is AND.
#   "|" between goal branches is OR.
#   _ is anonymous and is not printed in query output.
#
# Predicates:
#   math:add(lhs: left, rhs: right, result: out)
#   math:sub(lhs: left, rhs: right, result: out)
#   math:mul(lhs: left, rhs: right, result: out)
#   math:div(lhs: left, rhs: right, result: out)
#   math:mod(lhs: left, rhs: right, result: out)
#   str:len(data: value, equals: out)
#   str:contains(data: value, needle: needle)
#   str:concat(left: value, right: right, result: out)
#   str:lower(data: value, equals: out)
#   str:upper(data: value, equals: out)
#   array:get(data: array, position: index, access: out)
#   array:len(data: array, access: out)
#   array:push(data: array, value: value, result: out)
#   pair:first(data: pair, access: out)
#   pair:second(data: pair, access: out)
#   json:get(data: object, key: key, access: out)
#   json:parse(data: value, access: out)
#   throw(msg: reason)
#   throw(msg: reason, out: out)
#   throw(msg: value, target: HandlerRule)

# Native standard library signatures. These heads document the runtime contract;
# the bodies are C++ native functions.
#
#   console.readLine() -> string
#   console.write(value: any) -> "ok"
#   console.writeLine(value: any) -> "ok"
#   system.print(value: any) -> "ok"
#   type(value: any, name: out)
#   instanceof(value: any, type: TypeName)
#   file.readFile(path: string) -> string
#   file.writeFile(path: string, data: string) -> "ok"
#   file.appendFile(path: string, data: string) -> "ok"
#   file.exists(path: string) -> "true" | "false"
#   file.deleteFile(path: string) -> "true" | "false"
#   visualize.dataJson(loadImports: "true" | "false") -> string
#   visualize.graphJson(loadImports: "true" | "false") -> string
#   visualize.dataHtml(loadImports: "true" | "false") -> string
#   http.get(url: string) -> string
#   math.sqrt(value: number) -> number
#   math.pow(base: number, exponent: number) -> number
#   math.sin(value: number) -> number
#   math.cos(value: number) -> number
#   math.tan(value: number) -> number
#   math.log(value: number) -> number
#   math.exp(value: number) -> number
#   math.abs(value: number) -> number
#   math.floor(value: number) -> number
#   math.ceil(value: number) -> number
#   math.round(value: number) -> number
#   math.random(min: number, max: number) -> number
#   ml.sigmoid(value: number) -> number
#   ml.relu(value: number) -> number
#   ml.dot(left: [number], right: [number]) -> number
#   ml.meanSquaredError(left: [number], right: [number]) -> number

StdLib(name: "prelude").
