# JSON operations are declared in source and dispatched by the AST interpreter.

def json.parse(data: string) => ()
end
def json.get(data: any, key: string) => ()
end
def json.has(data: any, key: string) => ()
end
def json.keys(data: any) => ()
end
def json.set(data: any, key: string, value: any) => ()
end
def json.remove(data: any, key: string) => ()
end
def json.toText(data: any) => ()
end
