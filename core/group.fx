# Finite group operations use an explicit Cayley table:
# [{left: a, right: b, result: c}, ...]
# The AST interpreter dispatches these builtins directly.

def Group.validate(set: array, table: array, identity: any) => ()
def Group.closed(set: array, table: array) => ()
def Group.associative(set: array, table: array) => ()
def Group.identity(set: array, table: array, identity: any) => ()
def Group.inverse(set: array, table: array, identity: any) => ()
def Group.commutative(set: array, table: array) => ()
def Group.abelian(set: array, table: array, identity: any) => ()
