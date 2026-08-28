import ("group")

main() =>
    members := [0, 1],
    table := [
        {left: 0, right: 0, result: 0},
        {left: 0, right: 1, result: 1},
        {left: 1, right: 0, result: 1},
        {left: 1, right: 1, result: 0}
    ],
    return (
        validation: Group.validate(set: members, table: table, identity: 0),
        closed: Group.closed(set: members, table: table),
        associative: Group.associative(set: members, table: table),
        identity: Group.identity(set: members, table: table, identity: 0),
        inverse: Group.inverse(set: members, table: table, identity: 0),
        commutative: Group.commutative(set: members, table: table),
        abelian: Group.abelian(set: members, table: table, identity: 0)
    )
