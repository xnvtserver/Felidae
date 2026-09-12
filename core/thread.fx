# Native thread stdlib declarations. A thread runs a method on an independent
# interpreter snapshot and returns a handle that can be started and joined.

def thread.createThread(function: string) => ()
def thread.start(thread: any) => ()
def thread.pause(thread: any) => ()
def thread.stop(thread: any) => ()
def thread.status(thread: any) => ()
def thread.result(thread: any) => ()
