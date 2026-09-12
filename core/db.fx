# Direct fact-database source synchronization. Ordinary Type.insert,
# Type.where(...).update, and Type.where(...).delete are language operations
# and do not require importing this declaration.
db.sync(path: string) => ()
