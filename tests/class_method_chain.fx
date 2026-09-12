class Counter
    value: number

    def increment(amount: number) =>
        return Counter(value: self.value + amount)
    end

    def doubled() =>
        return Counter(value: self.value * 2)
    end
end

def main() =>
    counter := Counter(value: 2)
    return counter.increment(amount: 3).doubled().value
end
