# Native string stdlib declarations. Bodies are implemented by the native/runtime bridge.

def str.len(data: string, equals: number) => ()
end
def str.contains(data: string, needle: string, access: bool) => ()
end
def str.concat(left: string, right: string, result: string) => ()
end
def str.join(data: array, delimiter: string, result: string) => ()
end
def str.lower(data: string, equals: string) => ()
end
def str.upper(data: string, equals: string) => ()
end
def str.trim(data: string, access: string) => ()
end
def str.split(data: string, delimiter: string, access: array) => ()
end
def str.replace(data: string, search: string, replacement: string, access: string) => ()
end
def str.startsWith(data: string, prefix: string, access: bool) => ()
end
def str.endsWith(data: string, suffix: string, access: bool) => ()
end
