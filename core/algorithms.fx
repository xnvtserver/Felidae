import "math"

# Standard search algorithms implemented in Felidae itself.
# Both return a zero-based index, or -1 when the target is absent.

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
    array:get(data: data, position: index, access: current)
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
    array:get(data: data, position: middle, access: current)
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
