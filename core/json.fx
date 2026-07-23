# Native JSON stdlib declarations. Bodies are implemented by the native/runtime bridge.

json.parse(data: string, access: any) => ()
json.get(data: any, key: string, access: any) => ()
json.has(data: any, key: string, access: string) => ()
json.keys(data: any, access: array) => ()
json.set(data: any, key: string, value: any, access: any) => ()
json.remove(data: any, key: string, access: any) => ()
json.toText(data: any, access: string) => ()
