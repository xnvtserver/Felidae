import ("http", "json", "str", "system").

ServerRoute(method: "GET", path: "/", description: "Return the generated Felidae HTML page.").
ServerRoute(method: "POST", path: "/", description: "Return the same generated response for smoke tests.").
ServerRoute(method: "PUT", path: "/", description: "Return the same generated response for smoke tests.").
ServerRoute(method: "DELETE", path: "/", description: "Return the same generated response for smoke tests.").

DefaultServerConfig() =>
    return (host: "127.0.0.1", port: 8080, portText: "8080").

RouteSummary() =>
    routes := lambda(ServerRoute, route => {
        method: route.method,
        path: route.path,
        description: route.description
    }),
    return (routes).

RouteSummaryJson() =>
    routes := RouteSummary(),
    return (json.toText(data: routes)).

ServerBody(url: string) =>
    routeJson := RouteSummaryJson(),
    beforeUrl := "<main style='font-family:Segoe UI,Arial,sans-serif;max-width:760px;margin:40px auto;padding:24px;border:1px solid #dfe5f2;border-radius:8px'><h1>Felidae HTTP Server</h1><p>Running at ",
    withUrl := str.concat(left: beforeUrl, right: url),
    intro := str.concat(left: withUrl, right: "</p><p>This page is generated from Felidae facts and served through core/http.fx helpers.</p><h2>Routes</h2><pre style='background:#111827;color:#e5e7eb;padding:16px;border-radius:8px;overflow:auto'>"),
    withRoutes := str.concat(left: intro, right: routeJson),
    return (str.concat(left: withRoutes, right: "</pre></main>")).

ServerResponse(host: string, port: string) =>
    url := HttpUrl(host: host, port: port),
    body := ServerBody(url: url),
    page := HttpHtmlDocument(title: "Felidae HTTP Server", body: body),
    return (HttpHtmlResponse(body: page)).

main() =>
    config := DefaultServerConfig(),
    url := HttpUrl(host: config.host, port: config.portText),
    response := ServerResponse(host: config.host, port: config.portText),
    startup := system.print(value: str.concat(left: "Starting Felidae example server at ", right: url)),
    status := HttpServeResponse(host: config.host, port: config.port, response: response),
    return (
        url: url,
        status: status,
        startup: startup,
        routes: RouteSummary()
    ).
