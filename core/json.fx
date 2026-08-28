# JSON operations are compiler-known declarations. The compiler emits a
# Builtin IR instruction and RegisterVm calls src/form/libs/Json.cpp directly.

json.parse(data: string) => ()
json.get(data: any, key: string) => ()
json.has(data: any, key: string) => ()
json.keys(data: any) => ()
json.set(data: any, key: string, value: any) => ()
json.remove(data: any, key: string) => ()
json.toText(data: any) => ()
