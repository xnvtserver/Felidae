# Native system stdlib declarations. Bodies are implemented by the native/runtime bridge.

def system.print(value: any) => ()
def system.printf(format: string) => ()
def type(value: any, name: string) => ()
def instanceof(value: any, type: string) => ()
