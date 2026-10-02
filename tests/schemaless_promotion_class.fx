class Device
    key(id)
    index(reading)
    id: string
    reading: number

    def doubled() =>
        return this.reading * 2
    end
end

def main() =>
    stored := Device.where(id: "device-1").get(pos: 0)
    indexed := Device.where(reading: 21)
    return (count: indexed.count(), doubled: stored.doubled())
end
