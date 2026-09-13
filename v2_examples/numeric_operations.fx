# A tour of the numeric surface: native math.* builtins (bare `min`/`max`
# accept either one array or several values directly) and core/numeric.fx's
# plain functions (clamp/lerp/diff/avg/weightedAverage/square/cube/
# reciprocal/sign/trunc/inRange/isFinite/isNaN), all called with named
# arguments.
import "numeric"

def main() =>
    return (
        minimum: min(0.8, 0.3),
        maximum: max(0.8, 0.3),
        absolute: math.abs(value: -4.5),
        difference: diff(a: 8.2, b: 5.0),
        average: avg(a: 10.0, b: 20.0),
        weightedAverage: weightedAverage(a: 10.0, b: 20.0, weightA: 1.0, weightB: 3.0),
        clamped: clamp(value: 1.4, low: 0.0, high: 1.0),
        floor: math.floor(value: 4.8),
        ceil: math.ceil(value: 4.2),
        rounded: math.round(value: 4.6),
        truncated: trunc(value: 4.8),
        squareRoot: math.sqrt(value: 9.0),
        cubeRoot: math.cbrt(value: 8.0),
        power: math.pow(base: 2.0, exponent: 3.0),
        exponential: math.exp(value: 0.0),
        naturalLog: math.log(value: 1.0),
        decimalLog: math.log10(value: 1000.0),
        modulo: math.mod(lhs: 7.5, rhs: 2.0),
        interpolated: lerp(a: 10.0, b: 20.0, t: 0.25),
        sign: sign(value: -7.2),
        reciprocal: reciprocal(value: 4.0),
        square: square(value: 3.0),
        cube: cube(value: 2.0),
        inRange: inRange(value: 0.7, low: 0.0, high: 1.0),
        finite: isFinite(value: 1.5),
        nan: isNaN(value: 1.5)
    )
end
