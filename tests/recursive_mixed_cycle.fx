class First extends Second
    first_value: number
end

Second extend First(first_value: 1, second_value: 2)

def main() =>
    return Second.count()
end
