# Native math stdlib declarations. Bodies are implemented by the native/runtime bridge.

def math.pi() => ()
end
def math.e() => ()
end
def math.random(min: number, max: number) => ()
end
def math.pow(base: number, exponent: number) => ()
end
def math.atan2(y: number, x: number) => ()
end
def math.sqrt(value: number) => ()
end
def math.sin(value: number) => ()
end
def math.cos(value: number) => ()
end
def math.tan(value: number) => ()
end
def math.asin(value: number) => ()
end
def math.acos(value: number) => ()
end
def math.atan(value: number) => ()
end
def math.log(value: number) => ()
end
def math.log10(value: number) => ()
end
def math.exp(value: number) => ()
end
def math.abs(value: number) => ()
end
def math.floor(value: number) => ()
end
def math.ceil(value: number) => ()
end
def math.round(value: number) => ()
end
# Signed cube root (cbrt(-8) = -2): the one operation below that plain
# arithmetic cannot reproduce, since pow(value, 1/3) is undefined for
# negative bases in the reals.
def math.cbrt(value: number) => ()
end
