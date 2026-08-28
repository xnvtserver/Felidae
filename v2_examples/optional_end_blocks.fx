# `end` is an optional contextual block delimiter. It is useful when nested
# control flow would otherwise rely only on indentation. It creates no HIR or
# VM operation: the parser consumes it at method/if boundaries.

classify(value: number) =>
    if value > 10 then
        return 2.0
    else
        if value > 0 then
            return 1.0
        else
            return 0.0
        end
    end
end

main() =>
    return (
        high: classify(value: 20),
        middle: classify(value: 5),
        low: classify(value: -1)
    )
end
