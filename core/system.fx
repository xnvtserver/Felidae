# Native system stdlib declarations. Bodies are implemented by the native/runtime bridge.

def system.print(value: any) => ()
end
def system.printf(format: string) => ()
end
def type(value: any, name: string) => ()
end
def instanceof(value: any, type: string) => ()
end
