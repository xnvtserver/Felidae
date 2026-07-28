# Native system stdlib declarations. Bodies are implemented by the native/runtime bridge.

system.print(value: any) => ()
system.printf(format: string) => ()
type(value: any, name: string) => ()
instanceof(value: any, type: string) => ()
