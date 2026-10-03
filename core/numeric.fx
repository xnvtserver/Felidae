# Deterministic numeric utilities implemented as plain Felidae source rather
# than native builtins: every one of
# these is exactly reproducible with existing arithmetic, comparisons, and
# the min/max array builtins, so a second native implementation would only
# be a duplicate to keep in sync. math.cbrt (core/math.fx) stays native
# because it is the one operation here plain arithmetic cannot reproduce
# (pow(value, 1/3) is undefined for negative bases in the reals).
#
# Plain global names are intentional: dotted names identify class members or
# registered native libraries, not namespaces.

def clamp(value: number, low: number, high: number) =>
    return max([low, min([high, value])]).
end

def lerp(a: number, b: number, t: number) =>
    return a + (b - a) * t.
end

def diff(a: number, b: number) =>
    return math.abs(value: a - b).
end

def weightedAverage(a: number, b: number, weightA: number, weightB: number) =>
    where weightA + weightB == 0.
    total := a + b.
    return total / 2.
else
    weighted := a * weightA + b * weightB.
    totalWeight := weightA + weightB.
    return weighted / totalWeight.
end

def square(value: number) =>
    return value * value.
end

def cube(value: number) =>
    return value * value * value.
end

def reciprocal(value: number) =>
    where value == 0.
    return 0.
else
    return 1 / value.
end

def sign(value: number) =>
    if value > 0 then
        return 1.
    elif value < 0 then
        return -1.
    else
        return 0.
    end
end

def trunc(value: number) =>
    if value >= 0 then
        return math.floor(value: value).
    else
        return math.ceil(value: value).
    end
end

def inRange(value: number, low: number, high: number) =>
    return value >= low and value <= high.
end
