# Internal native library bridge.
# Public libraries import this file and call system_library_loader so native
# invocation stays centralized instead of being duplicated in every module.

system.flibrary.call(module: string, function: string, args: any) => ()

system_library_loader(module: string, function: string, args: any) =>
    dll_output := system.flibrary.call(module: module, function: function, args: args),
    return dll_output
