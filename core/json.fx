# JSON operations are declared in source and dispatched by the AST interpreter.

json.parse(data: string) => ()
json.get(data: any, key: string) => ()
json.has(data: any, key: string) => ()
json.keys(data: any) => ()
json.set(data: any, key: string, value: any) => ()
json.remove(data: any, key: string) => ()
json.toText(data: any) => ()
