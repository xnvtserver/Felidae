# CSV operations are compiler-known declarations. The compiler emits a
# Builtin IR instruction and RegisterVm calls src/form/libs/Csv.cpp directly.

csv.parse(data: string) => ()
csv.toFacts(data: string, type: string) => ()
csv.toText(data: array) => ()
csv.toFelidaeFacts(data: array, type: string) => ()
