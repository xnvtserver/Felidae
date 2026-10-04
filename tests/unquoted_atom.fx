class Label
    key(id).
    def id: string.
    def value: atom.
end

def Label(id: "label-1", value: unquoted_text).

def main() =>
    def stored := Label.where(id: "label-1").first().
    return (value: stored.value, atom: is_atom(stored.value)).
end
