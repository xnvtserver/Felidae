# Finite group operations use an explicit Cayley table:
# [{left: a, right: b, result: c}, ...]
# The AST interpreter dispatches these builtins directly.

Group.validate(set: array, table: array, identity: any) => ()
Group.closed(set: array, table: array) => ()
Group.associative(set: array, table: array) => ()
Group.identity(set: array, table: array, identity: any) => ()
Group.inverse(set: array, table: array, identity: any) => ()
Group.commutative(set: array, table: array) => ()
Group.abelian(set: array, table: array, identity: any) => ()
