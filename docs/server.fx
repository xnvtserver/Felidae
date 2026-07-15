import ("http" "str" "system" "html_components.fx" "docs_search.fx" "overview.fx" "basics.fx" "getting_started.fx" "syntax.fx" "facts.fx" "queries.fx" "methods.fx" "language_reference.fx" "stdlib.fx" "libraries.fx" "probability.fx" "native_modules.fx" "debugging.fx" "hosting.fx" "server_features.fx" "downloads.fx" "version.fx" "milestones.fx" "about.fx" "playground.fx").

DocumentationBody() =>
    overview := RenderSection(input: DocsOverview()),
    basics := RenderSection(input: DocsBasics()),
    gettingStarted := RenderSection(input: DocsGettingStarted()),
    syntax := RenderSection(input: DocsSyntax()),
    facts := RenderSection(input: DocsFacts()),
    queries := RenderSection(input: DocsQueries()),
    methods := RenderSection(input: DocsMethods()),
    reference := RenderSection(input: DocsLanguageReference()),
    stdlib := RenderSection(input: DocsStdlib()),
    libraries := RenderSection(input: DocsLibraries()),
    probability := RenderSection(input: DocsProbability()),
    nativeModules := RenderSection(input: DocsNativeModules()),
    debugging := RenderSection(input: DocsDebugging()),
    hosting := RenderSection(input: DocsHosting()),
    serverFeatures := RenderSection(input: DocsServerFeatures()),
    downloads := RenderSection(input: DocsDownloads()),
    version := RenderSection(input: DocsVersion()),
    milestones := RenderSection(input: DocsMilestones()),
    about := RenderSection(input: DocsAbout()),
    playground := RenderPlayground(input: DocsPlayground()),
    part1 := str.concat(left: overview, right: basics),
    part2 := str.concat(left: part1, right: gettingStarted),
    part3 := str.concat(left: part2, right: syntax),
    part4 := str.concat(left: part3, right: facts),
    part5 := str.concat(left: part4, right: queries),
    part6 := str.concat(left: part5, right: methods),
    part7 := str.concat(left: part6, right: reference),
    part8 := str.concat(left: part7, right: stdlib),
    part9 := str.concat(left: part8, right: libraries),
    part10 := str.concat(left: part9, right: probability),
    part11 := str.concat(left: part10, right: nativeModules),
    part12 := str.concat(left: part11, right: debugging),
    part13 := str.concat(left: part12, right: hosting),
    part14 := str.concat(left: part13, right: serverFeatures),
    part15 := str.concat(left: part14, right: downloads),
    part16 := str.concat(left: part15, right: version),
    part17 := str.concat(left: part16, right: milestones),
    part18 := str.concat(left: part17, right: about),
    return (str.concat(left: part18, right: playground)).

DocsServerConfig() =>
    return (
        host: "127.0.0.1",
        port: 8090,
        portText: "8090",
        publicPath: "/"
    ).

DocsServerUrl(config: any) =>
    return (HttpUrl(host: config.host, port: config.portText)).

PrintStartup(url: string) =>
    system.print(value: "Felidae documentation server starting..."),
    system.print(value: url),
    system.print(value: "Native C++ HTTP server is serving the generated Felidae documentation SPA."),
    system.print(value: "For Ubuntu hosting, keep this bound to 127.0.0.1 and proxy to it from Nginx."),
    system.print(value: "Press Ctrl+C in this terminal or stop the service manager unit to stop the server."),
    return (status: "printed").

main() =>
    config := DocsServerConfig(),
    body := DocumentationBody(),
    searchData := DocsSearchJson(),
    html := RenderHtmlShell(body: body, searchData: searchData),
    response := HttpHtmlResponse(body: html),
    url := DocsServerUrl(config: config),
    startup := PrintStartup(url: url),
    status := HttpServeResponse(
        host: config.host,
        port: config.port,
        response: response
    ),
    return (
        status: status,
        url: url,
        config: config,
        startup: startup
    ).
