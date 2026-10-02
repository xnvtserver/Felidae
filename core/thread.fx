# Native thread stdlib declarations. A thread runs a method on an independent
# interpreter snapshot and returns a handle that can be started and joined.

def thread.createThread(function: string) => ()
end
def thread.start(thread: any) => ()
end
def thread.pause(thread: any) => ()
end
def thread.stop(thread: any) => ()
end
def thread.status(thread: any) => ()
end
def thread.result(thread: any) => ()
end
