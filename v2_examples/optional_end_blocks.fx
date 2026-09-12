# `end` is an explicit contextual block delimiter for nested direct AST
# control flow. It adds no runtime operation.

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
