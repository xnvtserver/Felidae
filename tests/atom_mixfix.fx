@mixfix(pattern: 'echo {value: atom} with {expected: atom}')
def echoAtom() =>
    (value: value, equal: value = expected).
end

def main() =>
    def result := echo active with active.
    (value: result.value, equal: result.equal).
end
