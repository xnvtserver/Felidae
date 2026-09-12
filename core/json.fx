# JSON operations are declared in source and dispatched by the AST interpreter.

def json.parse(data: string) => ()
def json.get(data: any, key: string) => ()
def json.has(data: any, key: string) => ()
def json.keys(data: any) => ()
def json.set(data: any, key: string, value: any) => ()
def json.remove(data: any, key: string) => ()
def json.toText(data: any) => ()
