# Native array stdlib declarations. Bodies are implemented by the native/runtime bridge.

def array.get(data: array, position: number, access: any) => ()
def array.len(data: array, access: number) => ()
def array.push(data: array, value: any, result: array) => ()
