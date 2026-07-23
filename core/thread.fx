# Native thread stdlib declarations. A thread runs a method on an independent
# interpreter snapshot and returns a handle that can be started and joined.

thread.createThread(function: string) => ()
thread.start(thread: any) => ()
thread.pause(thread: any) => ()
thread.stop(thread: any) => ()
thread.status(thread: any) => ()
thread.result(thread: any) => ()
