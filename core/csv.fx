# CSV operations are declared in source and dispatched by the AST interpreter.

def csv.parse(data: string) => ()
end
def csv.toFacts(data: string, type: string) => ()
end
# The source-path overload records which file these facts came from, so a
# later Type.insert/update/delete operations write back only this file's facts
# instead of the whole store. Use this form for persistent facts.
def csv.toFacts(data: string, type: string, source: string) => ()
end
def csv.toText(data: array) => ()
end
def csv.toFelidaeFacts(data: array, type: string) => ()
end
