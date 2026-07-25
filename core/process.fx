import ("flibrary", "system.flibrary.process")

process.platform() =>
    return (system_library_loader(module: "process", function: "platform", args: {}))

process.exec(command: string) =>
    return (system_library_loader(module: "process", function: "exec", args: {command: command}))

process.sleep(milliseconds: int) =>
    return (system_library_loader(module: "process", function: "sleep", args: {milliseconds: milliseconds}))
