class employee
    key(id).
    def id: string.
    def state: atom.
    def payload: optional<string | number>.
end

def employee(id: "e1", state: active, payload: 7).

def FIND_ACTIVE(id: string) =>
    def employee(id: id, state: state, payload: payload).
    (
        matched: state = active,
        atom: is_atom(state),
        quoted_atom: is_atom('active state'),
        payload: payload
    ).
end

def missing: optional<string | number>.

FIND_ACTIVE(id: "e1").
