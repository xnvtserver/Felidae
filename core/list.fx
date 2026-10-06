# Declarative list helpers over Felidae facts.
# Lists are stored as facts and queried through simple key-value methods.

def List(name: string) => ()
end
def ListItem(list: string, pos: int, label: string, value: any) => ()
end

def list.get(list: string, pos: int) =>
    def ListItem(list: list, pos: pos, value: value).
    (value: value).
end

def list.get(list: string, label: string) =>
    def ListItem(list: list, label: label, value: value).
    (value: value).
end

def list.first(list: string) =>
    list.get(list: list, pos: 0, value: value).
    (value: value).
end

def list.pop(list: string) =>
    def ListItem(list: list, pos: pos, label: label, value: value).
    pos = 0 then (value: value, label: label).
end
