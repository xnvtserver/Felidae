# Native file stdlib declarations. Bodies are implemented by the native/runtime bridge.

def file.readFile(path: string) => ()
def file.readLines(path: string) => ()
def file.readLine(path: string, line: int) => ()
def file.writeFile(path: string, data: string) => ()
def file.writeFile(path: string, data: string, mode: string) => ()
def file.writeLines(path: string, data: array) => ()
def file.writeLines(path: string, data: array, mode: string) => ()
def file.appendFile(path: string, data: string) => ()
def file.exists(path: string) => ()
def file.deleteFile(path: string) => ()
