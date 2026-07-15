# Native string stdlib declarations. Bodies are implemented by the native/runtime bridge.

str.len(data: string, equals: number) => ().
str.contains(data: string, needle: string, access: string) => ().
str.concat(left: string, right: string, result: string) => ().
str.join(data: array, delimiter: string, result: string) => ().
str.lower(data: string, equals: string) => ().
str.upper(data: string, equals: string) => ().
str.trim(data: string, access: string) => ().
str.split(data: string, delimiter: string, access: array) => ().
str.replace(data: string, search: string, replacement: string, access: string) => ().
str.startsWith(data: string, prefix: string, access: string) => ().
str.endsWith(data: string, suffix: string, access: string) => ().
