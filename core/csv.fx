# CSV operations are declared in source and dispatched by the AST interpreter.

csv.parse(data: string) => ()
csv.toFacts(data: string, type: string) => ()
# The source-path overload records which file these facts came from, so a
# later Type.insert/update/delete operations write back only this file's facts
# instead of the whole store. Use this form for persistent facts.
csv.toFacts(data: string, type: string, source: string) => ()
csv.toText(data: array) => ()
csv.toFelidaeFacts(data: array, type: string) => ()
