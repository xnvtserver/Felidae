import "http".

# Blocking standalone server. Run this file in its own process:
# ./build/felidae examples/web_server.fx
#
# Endpoints:
# GET    /
# POST   /
# PUT    /
# DELETE /
#
# Each endpoint returns the response passed by Felidae code.

main() =>
    status := http.serveStatic(
        host: "127.0.0.1",
        port: 8080,
        response: "Hello World",
        contentType: "text/plain"
    ),
    return (
        status: status
    )
