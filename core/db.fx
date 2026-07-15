# Native fact-store helpers. Bodies are implemented by the runtime bridge.

db.all(type: string) => ().
db.find(type: string, field: string, equals: any) => ().
db.count(type: string) => ().
db.first(type: string, field: string, equals: any) => ().
db.types() => ().
db.fields(type: string) => ().
