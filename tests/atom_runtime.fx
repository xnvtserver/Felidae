def global_status: atom := approved.

def identity(value: any) =>
    value.
end

def main() =>
    def empty := nil.
    def status: atom := active.
    def values := [status, pending].
    def record := {state: status, label: "active"}.
    def pair := Pair(status, approved).
    (
        kind: type(status),
        string_type: type("active"),
        number_type: type(1),
        boolean_type: type(true),
        array_type: type(values),
        object_type: type(record),
        nil_type: type(empty),
        atom_check: is_atom(status),
        literal_check: is_atom(active),
        quoted_check: is_atom('active state'),
        string_check: is_atom("active"),
        number_check: is_atom(1),
        function_check: is_atom(identity.function),
        empty: empty,
        global: global_status,
        distinct: status != "active",
        passed: identity(value: status),
        list_value: values.get(pos: 1),
        map_value: record.state,
        term_value: pair
    ).
end
