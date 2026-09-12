# Native math stdlib declarations. Bodies are implemented by the native/runtime bridge.

def math.pi() => ()
def math.e() => ()
def math.random(min: number, max: number) => ()
def math.pow(base: number, exponent: number) => ()
def math.atan2(y: number, x: number) => ()
def math.sqrt(value: number) => ()
def math.sin(value: number) => ()
def math.cos(value: number) => ()
def math.tan(value: number) => ()
def math.asin(value: number) => ()
def math.acos(value: number) => ()
def math.atan(value: number) => ()
def math.log(value: number) => ()
def math.log10(value: number) => ()
def math.exp(value: number) => ()
def math.abs(value: number) => ()
def math.floor(value: number) => ()
def math.ceil(value: number) => ()
def math.round(value: number) => ()
# Signed cube root (cbrt(-8) == -2): the one operation below that plain
# arithmetic cannot reproduce, since pow(value, 1/3) is undefined for
# negative bases in the reals.
def math.cbrt(value: number) => ()
