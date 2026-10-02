# Direct fact-database source synchronization. Ordinary Type.insert,
# Type.where(...).update, and Type.where(...).delete are language operations
# and do not require importing this declaration.
def db.sync(path: string) => ()
end
def db.stats() => ()
end
def db.config() => ()
end
def db.configure(options: any) => ()
end
