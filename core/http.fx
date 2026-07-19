import "str".

# Native HTTP stdlib declarations. Bodies are implemented by the native/runtime bridge.

http.get(url: string) => ().
http.post(url: string, body: string) => ().
http.post(url: string, body: string, contentType: string) => ().
http.put(url: string, body: string) => ().
http.put(url: string, body: string, contentType: string) => ().
http.delete(url: string) => ().
http.serveStatic(host: string, port: int, response: string) => ().
http.serveStatic(host: string, port: int, response: string, contentType: string) => ().

# Felidae-level helpers. These keep application code data-shaped while the
# native bridge remains responsible for network I/O.

HttpResponse(status: int, contentType: string, body: string) =>
    return (
        status: status,
        contentType: contentType,
        body: body
    ).

HttpTextResponse(body: string) =>
    return (HttpResponse(status: 200, contentType: "text/plain", body: body)).

HttpHtmlResponse(body: string) =>
    return (HttpResponse(status: 200, contentType: "text/html", body: body)).

HttpJsonResponse(body: string) =>
    return (HttpResponse(status: 200, contentType: "application/json", body: body)).

HttpNotFoundResponse(body: string) =>
    return (HttpResponse(status: 404, contentType: "text/plain", body: body)).

HttpUrl(host: string, port: string) =>
    prefix := str.concat(left: "http://", right: host),
    withPort := str.concat(left: prefix, right: ":"),
    portText := str.concat(left: withPort, right: port),
    return (str.concat(left: portText, right: "/")).

HttpHtmlDocument(title: string, body: string) =>
    headStart := "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>",
    headTitle := str.concat(left: headStart, right: title),
    headEnd := str.concat(left: headTitle, right: "</title></head><body>"),
    withBody := str.concat(left: headEnd, right: body),
    return (str.concat(left: withBody, right: "</body></html>")).

HttpServeResponse(host: string, port: int, response: any) =>
    return (http.serveStatic(
        host: host,
        port: port,
        response: response.body,
        contentType: response.contentType
    )).

HttpServeText(host: string, port: int, body: string) =>
    response := HttpTextResponse(body: body),
    return (HttpServeResponse(host: host, port: port, response: response)).

HttpServeHtml(host: string, port: int, body: string) =>
    response := HttpHtmlResponse(body: body),
    return (HttpServeResponse(host: host, port: port, response: response)).

HttpServeJson(host: string, port: int, body: string) =>
    response := HttpJsonResponse(body: body),
    return (HttpServeResponse(host: host, port: port, response: response)).
