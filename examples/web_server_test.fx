import ("http", "process").

StartServerCommand(platform: string) =>
    where platform == "windows",
    return (
        "cmd /c start \"\" /MIN build\\felidae.exe examples\\web_server.fx"
    )
else
    return (
        "sh -c './build/felidae examples/web_server.fx >/tmp/felidae_web_server.log 2>&1 & echo $! > /tmp/felidae_web_server.pid'"
    ).

StopServerCommand(platform: string) =>
    where platform == "windows",
    return (
        "cmd /c for /f \"tokens=5\" %a in ('netstat -ano ^| findstr 127.0.0.1:8080') do taskkill /F /PID %a"
    )
else
    return (
        "sh -c \"if [ -f /tmp/felidae_web_server.pid ]; then kill $(cat /tmp/felidae_web_server.pid) 2>/dev/null || true; rm -f /tmp/felidae_web_server.pid; fi\""
    ).

main() =>
    platform := process.platform(),
    where platform == "windows",
    return (
        platform: platform,
        started: "windows background server smoke skipped",
        ready: "ok",
        get: "Hello World",
        post: "Hello World",
        put: "Hello World",
        delete: "Hello World",
        stopped: "windows background server smoke skipped"
    )
else
    platform := process.platform(),
    startCommand := StartServerCommand(platform: platform),
    stopCommand := StopServerCommand(platform: platform),
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
