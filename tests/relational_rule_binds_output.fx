# Variables inside a relational rule are renamed apart for every expansion.
# They must still bind: the caller's variable receives the fact value instead
# of staying unbound (v = v) and the solution count is not multiplied.
class Candidate
    key(id).
    id: string.
end

Candidate(id: "a").
Candidate(id: "b").

def pick(x, y) =>
    Candidate(id: x).
    Candidate(id: y).
end
