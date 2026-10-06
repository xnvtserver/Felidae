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
    max([low, min([high, value])]).
end

def lerp(a: number, b: number, t: number) =>
    a + (b - a) * t.
end

def diff(a: number, b: number) =>
    math.abs(value: a - b).
end

def weightedAverage(a: number, b: number, weightA: number, weightB: number) =>
    weightA + weightB = 0 then (a + b) / 2
    else (a * weightA + b * weightB) / (weightA + weightB).
end

def square(value: number) =>
    value * value.
end

def cube(value: number) =>
    value * value * value.
end

def reciprocal(value: number) =>
    value = 0 then 0 else 1 / value.
end

def sign(value: number) =>
    value > 0 then 1
    else value < 0 then -1
    else 0.
end

def trunc(value: number) =>
    value >= 0 then math.floor(value: value)
    else math.ceil(value: value).
end

def inRange(value: number, low: number, high: number) =>
    value >= low and value <= high.
end
