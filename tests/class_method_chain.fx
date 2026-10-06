class Counter
    def value: number.

    def increment(amount: number) =>
        Counter(value: this.value + amount).
    end

    def doubled() =>
        Counter(value: this.value * 2).
    end
end

def main() =>
    def counter := Counter(value: 2).
    counter.increment(amount: 3).doubled().value.
end
