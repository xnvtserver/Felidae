import ("http", "json")

ApiContract := {
    service: "Felidae Facts DB API",
    storage: "facts",
    note: "Current MVP serves this endpoint contract statically. A host process can map these endpoints to felidae.exe command files.",
    endpoints: [
        {
            method: "GET",
            path: "/customers?city=SEA&status=active",
            command: "build/felidae.exe examples/fact_api/api_query.fx",
            description: "Query facts with SQL-like conditions implemented as Felidae rules."
        },
        {
            method: "POST",
            path: "/customers",
            temp_file: "examples/fact_api/temp_insert.fx",
            command: "build/felidae.exe examples/fact_api/api_insert.fx",
            description: "Write request body as a temp fact file, then append it into the fact store output."
        },
        {
            method: "PUT",
            path: "/customers/c004",
            command: "build/felidae.exe examples/fact_api/api_update.fx",
            description: "Rewrite matching fact records only; methods and procedures are not modified."
        },
        {
            method: "DELETE",
            path: "/customers?status=inactive",
            command: "build/felidae.exe examples/fact_api/api_delete.fx",
            description: "Rewrite the fact file without facts matching delete conditions."
        }
    ]
}

main() =>
    response := json.toText(data: ApiContract)
    status := http.serveStatic(
        host: "127.0.0.1",
        port: 8090,
        response: response,
        contentType: "application/json"
    )
    return (
        status: status
    )
