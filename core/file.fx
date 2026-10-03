# Native file stdlib declarations. Bodies are implemented by the native/runtime bridge.

def file.readFile(path: string) => ()
end
def file.readLines(path: string) => ()
end
def file.readLine(path: string, line: int) => ()
end
def file.writeFile(path: string, data: string) => ()
end
def file.writeFile(path: string, data: string, mode: string) => ()
end
def file.writeLines(path: string, data: array) => ()
end
def file.writeLines(path: string, data: array, mode: string) => ()
end
def file.appendFile(path: string, data: string) => ()
end
def file.exists(path: string) => ()
end
def file.deleteFile(path: string) => ()
end
