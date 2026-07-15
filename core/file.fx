# Native file stdlib declarations. Bodies are implemented by the native/runtime bridge.

file.readFile(path: string) => ().
file.readLines(path: string) => ().
file.readLine(path: string, line: int) => ().
file.writeFile(path: string, data: string) => ().
file.writeFile(path: string, data: string, mode: string) => ().
file.writeLines(path: string, data: array) => ().
file.writeLines(path: string, data: array, mode: string) => ().
file.appendFile(path: string, data: string) => ().
file.exists(path: string) => ().
file.deleteFile(path: string) => ().
