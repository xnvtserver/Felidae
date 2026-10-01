# Native array stdlib declarations. Bodies are implemented by the native/runtime bridge.

def array.get(data: array, position: number, access: any) => ()
end
def array.len(data: array, access: number) => ()
end
def array.push(data: array, value: any, result: array) => ()
end
