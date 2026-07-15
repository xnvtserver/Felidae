# Felidae Docs Fact DB

This folder is used by the documentation fact-database examples.

Run the snippets from the repository root with the native interpreter when you
want to create, update, delete, or query files on disk:

```powershell
.\build\felidae.exe docs\db\fact_insert.fx
.\build\felidae.exe docs\db\fact_update.fx
.\build\felidae.exe docs\db\fact_query.fx
.\build\felidae.exe docs\db\fact_delete.fx
```

Browser WASM snippets can demonstrate the data flow, but host filesystem writes
require the native Felidae binary.
