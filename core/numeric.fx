# Deterministic numeric utilities, ported from the removed VM's
# NumericOperation set (form/NumericOperation.h,
# form/RegisterVm.cpp:evaluateNumericOperation in git history before
# e32c679) as plain Felidae source rather than native builtins: every one of
# these is exactly reproducible with existing arithmetic, comparisons, and
# the min/max array builtins, so a second native implementation would only
# be a duplicate to keep in sync. math.cbrt (core/math.fx) stays native
# because it is the one operation here plain arithmetic cannot reproduce
# (pow(value, 1/3) is undefined for negative bases in the reals).
#
# Plain (undotted) names throughout - see core/fuzzy.fx's header comment for
# why: a dotted name only resolves as a call through a registered native
# builtin or the small fixed fluent-operation whitelist (where/limit/join/
# ...), never through an ordinary declared clause, so "numeric.clamp(...)"
# would fail to parse as a call the same way Logic.negate and math.clamp
# already do.

clamp(value: number, low: number, high: number) =>
    return max([low, min([high, value])])

lerp(a: number, b: number, t: number) =>
    return a + (b - a) * t

diff(a: number, b: number) =>
    return math.abs(value: a - b)

weightedAverage(a: number, b: number, weightA: number, weightB: number) =>
    where weightA + weightB == 0
    total := a + b
    return total / 2
else
    weighted := a * weightA + b * weightB
    totalWeight := weightA + weightB
    return weighted / totalWeight

square(value: number) =>
    return value * value

cube(value: number) =>
    return value * value * value

reciprocal(value: number) =>
    where value == 0
    return 0
else
    return 1 / value

sign(value: number) =>
    if value > 0 then
        return 1
    elif value < 0 then
        return -1
    else
        return 0
    end
end

trunc(value: number) =>
    if value >= 0 then
        return math.floor(value: value)
    else
        return math.ceil(value: value)
    end
end

inRange(value: number, low: number, high: number) =>
    return value >= low and value <= high
