class Device
    key(id).
    index(reading).
    def id: string.
    def reading: number.

    def doubled() =>
        return this.reading * 2.
    end
end

def main() =>
    def stored := Device.where(id: "device-1").get(pos: 0).
    def indexed := Device.where(reading: 21).
    return (count: indexed.count(), doubled: stored.doubled()).
end
