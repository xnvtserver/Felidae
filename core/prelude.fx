# Auto-available expression builtins. These are implemented directly by the
# runtime and intentionally not declared as empty methods here, because an empty
# method declaration would shadow the expression builtin when imported.
#
# count(value)
# sum(array)
# average(array)
# min(array)
# max(array)
# sort(array)
# search(value, query)
# contains(value, query)
# lower(value)
# upper(value)
# length(value)

StdLib(name: "prelude").
