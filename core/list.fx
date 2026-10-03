# Declarative list helpers over Felidae facts.
# Lists are stored as facts and queried through simple key-value methods.

def List(name: string) => ()
end
def ListItem(list: string, pos: int, label: string, value: any) => ()
end

def list.get(list: string, pos: int) =>
    ListItem(list: list, pos: pos, value: value).
    return (value: value).
end

def list.get(list: string, label: string) =>
    ListItem(list: list, label: label, value: value).
    return (value: value).
end

def list.first(list: string) =>
    list.get(list: list, pos: 0, value: value).
    return (value: value).
end

def list.pop(list: string) =>
    ListItem(list: list, pos: pos, label: label, value: value).
    if pos == 0 then
    return (value: value, label: label).
    end
end
