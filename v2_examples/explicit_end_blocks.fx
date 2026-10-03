# `end` is a mandatory contextual block delimiter for functions and nested
# control flow. It adds no runtime operation.

def classify(value: number) =>
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

def main() =>
    return (
        high: classify(value: 20),
        middle: classify(value: 5),
        low: classify(value: -1)
    )
end
