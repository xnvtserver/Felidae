import ("http" "str" "system" "html_components.fx" "docs_search.fx" "overview.fx" "basics.fx" "getting_started.fx" "syntax.fx" "facts.fx" "queries.fx" "methods.fx" "data_structures_algorithms.fx" "testing.fx" "language_reference.fx" "stdlib.fx" "libraries.fx" "library_pages.fx" "probability.fx" "native_modules.fx" "debugging.fx" "hosting.fx" "server_features.fx" "download_page.fx" "version.fx" "milestones.fx" "about.fx" "playground.fx").

DocumentationSections() =>
    return ([
        RenderSection(input: DocsOverview()),
        RenderSection(input: DocsBasics()),
        RenderSection(input: DocsGettingStarted()),
        RenderSection(input: DocsSyntax()),
        RenderSection(input: DocsFacts()),
        RenderSection(input: DocsQueries()),
        RenderSection(input: DocsMethods()),
        RenderSection(input: DocsDataStructuresAlgorithms()),
        RenderSection(input: DocsTesting()),
        RenderSection(input: DocsLanguageReference()),
        RenderSection(input: DocsStdlib()),
        RenderSection(input: DocsLibraries()),
        RenderSection(input: DocsLibraryStr()),
        RenderSection(input: DocsLibraryArray()),
        RenderSection(input: DocsLibraryDb()),
        RenderSection(input: DocsLibraryJson()),
        RenderSection(input: DocsLibraryCsv()),
        RenderSection(input: DocsLibraryFile()),
        RenderSection(input: DocsLibraryHttp()),
        RenderSection(input: DocsLibraryMath()),
        RenderSection(input: DocsLibraryMl()),
        RenderSection(input: DocsLibrarySystem()),
        RenderSection(input: DocsLibraryConsole()),
        RenderSection(input: DocsLibraryProcess()),
        RenderSection(input: DocsLibraryThread()),
        RenderSection(input: DocsProbability()),
        RenderSection(input: DocsNativeModules()),
        RenderSection(input: DocsDebugging()),
        RenderSection(input: DocsHosting()),
        RenderSection(input: DocsServerFeatures()),
        RenderSection(input: DocsDownloadPage()),
        RenderSection(input: DocsVersion()),
        RenderSection(input: DocsMilestones()),
        RenderSection(input: DocsAbout()),
        RenderPlayground(input: DocsPlayground())
    ]).

DocumentationBody() =>
    sections := DocumentationSections(),
    return (str.join(data: sections, delimiter: "")).

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
