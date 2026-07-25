import ("csv", "process")

main() =>
    rows := csv.parse(data: "name,role\nAlice,Engineer\n")
    platform := process.platform()
    return (rows: rows, platform: platform)
