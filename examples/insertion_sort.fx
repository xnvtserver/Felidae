import ("array", "exception")

AppendRemaining(data: array, index: number, size: number, result: array) =>
    if index >= size then
        return result
    else
        value := array.get(data: data, position: index)
        next := array.push(data: result, value: value)
        return AppendRemaining(
            data: data,
            index: index + 1,
            size: size,
            result: next
        )

InsertValue(sorted: array, value: number, index: number, size: number, result: array) =>
    if index >= size then
        appended := array.push(data: result, value: value)
        return appended
    else
        current := array.get(data: sorted, position: index)
        if value <= current then
            withValue := array.push(data: result, value: value)
            return AppendRemaining(
                data: sorted,
                index: index,
                size: size,
                result: withValue
            )
        else
            withCurrent := array.push(data: result, value: current)
            return InsertValue(
                sorted: sorted,
                value: value,
                index: index + 1,
                size: size,
                result: withCurrent
            )

InsertionSortFrom(data: array, index: number, size: number, sorted: array) =>
    if index >= size then
        return sorted
    else
        value := array.get(data: data, position: index)
        next := InsertValue(
            sorted: sorted,
            value: value,
            index: 0,
            size: count(sorted),
            result: []
        )
        return InsertionSortFrom(
            data: data,
            index: index + 1,
            size: size,
            sorted: next
        )

InsertionSort(data: array) =>
    size := count(data)
    if size > 8 then
        rejected := exception.failure(
            kind: "SortLimitExceeded",
            message: "This example accepts at most eight values",
            source: "insertion_sort"
        )
        return {ok: rejected.ok, data: [], error: rejected.error}
    else
        sorted := InsertionSortFrom(data: data, index: 0, size: size, sorted: [])
        return {ok: true, data: sorted, error: nil}

main() =>
    sorted := InsertionSort(data: [7, 3, 9, 1, 3, 5])
    rejected := InsertionSort(data: [9, 8, 7, 6, 5, 4, 3, 2, 1])
    return (sorted: sorted, rejected: rejected)
