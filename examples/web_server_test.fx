import ("http" "process").

StartServerCommand() =>
    return (
        "sh -c './build/felidae examples/web_server.fx >/tmp/felidae_web_server.log 2>&1 & echo $! > /tmp/felidae_web_server.pid'"
    ).

StopServerCommand() =>
    return (
        "sh -c \"if [ -f /tmp/felidae_web_server.pid ]; then kill $(cat /tmp/felidae_web_server.pid) 2>/dev/null || true; rm -f /tmp/felidae_web_server.pid; fi\""
    ).

main() =>
    platform := process.platform(),
    startCommand := StartServerCommand(),
    stopCommand := StopServerCommand(),
    started := process.exec(command: startCommand),
    ready := process.sleep(milliseconds: 800),
    get := http.get(url: "http://127.0.0.1:8080/"),
    post := http.post(url: "http://127.0.0.1:8080/", body: "hello"),
    put := http.put(url: "http://127.0.0.1:8080/", body: "hello"),
    deleted := http.delete(url: "http://127.0.0.1:8080/"),
    stopped := process.exec(command: stopCommand),
    return (
        platform: platform,
        started: started,
        ready: ready,
        get: get,
        post: post,
        put: put,
        delete: deleted,
        stopped: stopped
    ).
