import ("flibrary", "system.flibrary.csv")

csv.parse(data: string) =>
    return (system_library_loader(module: "csv", function: "parse", args: {data: data}))

csv.toFacts(data: string, type: string) =>
    return (system_library_loader(module: "csv", function: "toFacts", args: {data: data, type: type}))

csv.toText(data: array) =>
    return (system_library_loader(module: "csv", function: "toText", args: {data: data}))

csv.toFelidaeFacts(data: array, type: string) =>
    return (system_library_loader(module: "csv", function: "toFelidaeFacts", args: {data: data, type: type}))
