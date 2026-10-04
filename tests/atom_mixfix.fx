@mixfix(pattern: "echo {value: atom} with {expected: atom}")
def echoAtom() =>
    return (value: value, equal: value = expected).
end

def main() =>
    def result := echo active with active.
    return (value: result.value, equal: result.equal).
end
