class Counter
    def value: number.

    def increment(amount: number) =>
        return Counter(value: this.value + amount).
    end

    def doubled() =>
        return Counter(value: this.value * 2).
    end
end

def main() =>
    def counter := Counter(value: 2).
    return counter.increment(amount: 3).doubled().value.
end
