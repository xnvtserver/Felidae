# Native string stdlib declarations. Bodies are implemented by the native/runtime bridge.

def str.len(data: string, equals: number) => ()
def str.contains(data: string, needle: string, access: bool) => ()
def str.concat(left: string, right: string, result: string) => ()
def str.join(data: array, delimiter: string, result: string) => ()
def str.lower(data: string, equals: string) => ()
def str.upper(data: string, equals: string) => ()
def str.trim(data: string, access: string) => ()
def str.split(data: string, delimiter: string, access: array) => ()
def str.replace(data: string, search: string, replacement: string, access: string) => ()
def str.startsWith(data: string, prefix: string, access: bool) => ()
def str.endsWith(data: string, suffix: string, access: bool) => ()
