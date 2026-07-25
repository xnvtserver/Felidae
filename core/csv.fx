import ("flibrary", "system.flibrary.csv")

csv.parse(data: string) =>
    return (system_library_loader(module: "csv", function: "parse", args: {data: data}))

csv.toFacts(data: string, type: string) =>
    return (system_library_loader(module: "csv", function: "toFacts", args: {data: data, type: type}))

csv.toText(data: array) =>
    return (system_library_loader(module: "csv", function: "toText", args: {data: data}))

csv.toFelidaeFacts(data: array, type: string) =>
    return (system_library_loader(module: "csv", function: "toFelidaeFacts", args: {data: data, type: type}))

csv.addRow(data: array, row: any) =>
    return (system_library_loader(module: "csv", function: "addRow", args: {data: data, row: row}))

csv.findRows(data: array, key: string, value: any) =>
    return (system_library_loader(module: "csv", function: "findRows", args: {data: data, key: key, value: value}))

csv.updateRows(data: array, key: string, value: any, patch: any) =>
    return (system_library_loader(module: "csv", function: "updateRows", args: {data: data, key: key, value: value, patch: patch}))

csv.deleteRows(data: array, key: string, value: any) =>
    return (system_library_loader(module: "csv", function: "deleteRows", args: {data: data, key: key, value: value}))
