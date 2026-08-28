# Optional `end` delimiters make every nested decision and method boundary
# explicit. The existing `then` pipeline remains an expression; `end` adds no
# executable operation and is consumed while constructing compiler HIR.

increment(value: number) =>
    return value + 1
end

double(value: number) =>
    return value * 2
end

evaluate(value: number) =>
    if value >= 0 then
        return increment(value: value)
            then double(value: system.result)
    else
        return 0.0
    end
end

main() =>
    return (
        accepted: evaluate(value: 4),
        rejected: evaluate(value: -4)
    )
end
