import "math"

# Lowercase procedure names throughout: a capitalized name called with named
# arguments triggers Felidae's implicit fact-construction heuristic instead
# of calling a same-named declared procedure (see
# v2_examples/where_guard_chain.fx and operator_expression_inventory.fx for
# the same fix on the same issue).
searchData := [
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63
]

linearSearchFrom(data: array, target: number, index: number, size: number) =>
    where index >= size
    return -1
else
    return linearSearchAt(
        data: data,
        target: target,
        index: index,
        size: size
    )

linearSearchAt(data: array, target: number, index: number, size: number) =>
    current := array:get(data: data, position: index)
    where current == target
    return index
else
    return linearSearchFrom(
        data: data,
        target: target,
        index: index + 1,
        size: size
    )

linearSearch(data: array, target: number) =>
    return linearSearchFrom(
        data: data,
        target: target,
        index: 0,
        size: count(data: data)
    )

binarySearchBetween(data: array, target: number, low: number, high: number) =>
    where low > high
    return -1
else
    return binarySearchAt(
        data: data,
        target: target,
        low: low,
        high: high
    )

binarySearchAt(data: array, target: number, low: number, high: number) =>
    middle := math.floor(value: (low + high) / 2)
    current := array:get(data: data, position: middle)
    where current == target
    return middle
else
    # A where-guarded clause only carries one else branch (see
    # desugarWhereGuardedClauses); the second, inner two-way choice is an
    # ordinary nested if/then/else instead of a second where guard.
    if current < target then
        return binarySearchBetween(
            data: data,
            target: target,
            low: middle + 1,
            high: high
        )
    else
        return binarySearchBetween(
            data: data,
            target: target,
            low: low,
            high: middle - 1
        )

binarySearch(data: array, target: number) =>
    return binarySearchBetween(
        data: data,
        target: target,
        low: 0,
        high: count(data: data) - 1
    )

linearSearchLast() =>
    return linearSearch(data: searchData, target: 63)

binarySearchLast() =>
    return binarySearch(data: searchData, target: 63)

searchMissing() =>
    return (
        linear: linearSearch(data: searchData, target: 100),
        binary: binarySearch(data: searchData, target: 100)
    )

main() =>
    return (
        linear_last: linearSearchLast(),
        binary_last: binarySearchLast(),
        missing: searchMissing()
    )
