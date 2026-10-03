# A value call whose body fails after an effect is false; the body is not
# evaluated a second time to build a truth tuple, so EFFECT prints once.
import "console".

def noisy(x: number) =>
    console.writeLine(value: "EFFECT").
    x > 5.
    return x.
end

def main() =>
    return noisy(1).
end
