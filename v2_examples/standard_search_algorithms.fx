import "math"

SearchData := [
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63
]

LinearSearchFrom(data: array, target: target, index: int, size: int) =>
    index >= size
        return -1
    else
        return LinearSearchAt(
            data: data,
            target: target,
            index: index,
            size: size
        )

LinearSearchAt(data: array, target: target, index: int, size: int) =>
    current := array:get(data: data, position: index)
    current == target
        return index
    else
        return LinearSearchFrom(
            data: data,
            target: target,
            index: index + 1,
            size: size
        )

LinearSearch(data: array, target: target) =>
    return LinearSearchFrom(
        data: data,
        target: target,
        index: 0,
        size: count(data)
    )

BinarySearchBetween(data: array, target: target, low: int, high: int) =>
    low > high
        return -1
    else
        return BinarySearchAt(
            data: data,
            target: target,
            low: low,
            high: high
        )

BinarySearchAt(data: array, target: target, low: int, high: int) =>
    middle := math.floor(value: (low + high) / 2)
    current := array:get(data: data, position: middle)
    current == target
        return middle
    else
        current < target
            return BinarySearchBetween(
                data: data,
                target: target,
                low: middle + 1,
                high: high
            )
        else
            return BinarySearchBetween(
                data: data,
                target: target,
                low: low,
                high: middle - 1
            )

BinarySearch(data: array, target: target) =>
    return BinarySearchBetween(
        data: data,
        target: target,
        low: 0,
        high: count(data) - 1
    )

LinearSearchLast() =>
    return LinearSearch(data: SearchData, target: 63)

BinarySearchLast() =>
    return BinarySearch(data: SearchData, target: 63)

SearchMissing() =>
    return (
        linear: LinearSearch(data: SearchData, target: 100),
        binary: BinarySearch(data: SearchData, target: 100)
    )

main() =>
    return (
        linear_last: LinearSearchLast(),
        binary_last: BinarySearchLast(),
        missing: SearchMissing()
    )
