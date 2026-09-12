# CSV operations are declared in source and dispatched by the AST interpreter.

def csv.parse(data: string) => ()
def csv.toFacts(data: string, type: string) => ()
# The source-path overload records which file these facts came from, so a
# later Type.insert/update/delete operations write back only this file's facts
# instead of the whole store. Use this form for persistent facts.
def csv.toFacts(data: string, type: string, source: string) => ()
def csv.toText(data: array) => ()
def csv.toFelidaeFacts(data: array, type: string) => ()
