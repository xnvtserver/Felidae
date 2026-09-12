class Counter
    value: number

    increment(amount: number) =>
        return Counter(value: self.value + amount)
    end

    doubled() =>
        return Counter(value: self.value * 2)
    end
end

main() =>
    counter := Counter(value: 2)
    return counter.increment(amount: 3).doubled().value
end
