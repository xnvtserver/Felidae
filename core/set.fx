import ("flibrary", "system.flibrary.set")

# Set mathematics is implemented by the independent native_modules/set package.
# Arrays remain supported for interop.  FactSelection inputs are accepted by
# the runtime; cardinality is evaluated directly against the selection cursor.

Set.union(sets: array) =>
    return (system_library_loader(module: "set", function: "union", args: {sets: sets}))

Set.intersection(sets: array) =>
    return (system_library_loader(module: "set", function: "intersection", args: {sets: sets, fields: []}))

Set.intersectionBy(sets: array, fields: array) =>
    return (system_library_loader(module: "set", function: "intersection", args: {sets: sets, fields: fields}))

Set.difference(sets: array) =>
    return (system_library_loader(module: "set", function: "difference", args: {sets: sets, fields: []}))

Set.differenceBy(sets: array, fields: array) =>
    return (system_library_loader(module: "set", function: "difference", args: {sets: sets, fields: fields}))

Set.symmetricDifference(sets: array) =>
    return (system_library_loader(module: "set", function: "symmetricDifference", args: {sets: sets, fields: []}))

Set.symmetricDifferenceBy(sets: array, fields: array) =>
    return (system_library_loader(module: "set", function: "symmetricDifference", args: {sets: sets, fields: fields}))

Set.equals(sets: array) =>
    return (system_library_loader(module: "set", function: "equals", args: {sets: sets, fields: []}))

Set.equalsBy(sets: array, fields: array) =>
    return (system_library_loader(module: "set", function: "equals", args: {sets: sets, fields: fields}))

Set.subset(sets: array) =>
    return (system_library_loader(module: "set", function: "subset", args: {sets: sets, fields: []}))

Set.subsetBy(sets: array, fields: array) =>
    return (system_library_loader(module: "set", function: "subset", args: {sets: sets, fields: fields}))

Set.superset(sets: array) =>
    return (system_library_loader(module: "set", function: "superset", args: {sets: sets, fields: []}))

Set.disjoint(sets: array) =>
    return (system_library_loader(module: "set", function: "disjoint", args: {sets: sets, fields: []}))

Set.disjointBy(sets: array, fields: array) =>
    return (system_library_loader(module: "set", function: "disjoint", args: {sets: sets, fields: fields}))

Set.cardinality(set: any) =>
    return (system_library_loader(module: "set", function: "cardinality", args: {sets: [set]}))

Set.contains(set: any, value: any) =>
    return (system_library_loader(module: "set", function: "contains", args: {sets: [set], value: value, fields: []}))

Set.containsBy(set: any, value: any, fields: array) =>
    return (system_library_loader(module: "set", function: "contains", args: {sets: [set], value: value, fields: fields}))
