# Variables inside a relational rule are renamed apart for every expansion.
# They must still bind: the caller's variable receives the fact value instead
# of staying unbound (v = v) and the solution count is not multiplied.
class Candidate
    key(id).
    def id: string.
end

def Candidate(id: "a").
def Candidate(id: "b").

def pick(x, y) =>
    def Candidate(id: x).
    def Candidate(id: y).
end
