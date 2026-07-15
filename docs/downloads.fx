import ("html_components.fx" "str").

DownloadCard(title: string, badge: string, text: string, href: string, label: string) =>
    icon := RenderHtmlTag(name: "span", id: "", class: "download-mark", content: badge),
    intro := RenderParagraph(text: text),
    action := RenderAnchor(input: HtmlAnchorData(label: label, href: href, class: "tool-button")),
    body1 := str.concat(left: icon, right: intro),
    body2 := str.concat(left: body1, right: action),
    return (RenderMiniCardContent(input: HtmlCardContentData(title: title, content: body2))).

DocsDownloads() =>
    windows := DownloadCard(title: "Windows", badge: "WIN", text: "Download felidae.exe and celidae.exe for Windows, or build them locally when release assets are not published yet.", href: "https://github.com/vishalkrishnaag/logicPrompts/releases", label: "Get Windows Binaries"),
    linux := DownloadCard(title: "Linux", badge: "LIN", text: "Download Linux binaries from the release page when available. Local builds should keep the same Felidae and Celidae split.", href: "https://github.com/vishalkrishnaag/logicPrompts/releases", label: "Get Linux Binaries"),
    macos := DownloadCard(title: "macOS", badge: "MAC", text: "Download macOS binaries from the release page when available. If no asset exists, build from source with the documented compiler command.", href: "https://github.com/vishalkrishnaag/logicPrompts/releases", label: "Get macOS Binaries"),
    release := DownloadCard(title: "GitHub Releases", badge: "Fx", text: "Use the release page as the stable generic download path for Felidae, Celidae, editor packages, checksums, and future tagged builds.", href: "https://github.com/vishalkrishnaag/logicPrompts/releases", label: "Open Releases"),
    binaries1 := str.concat(left: windows, right: linux),
    binaries2 := str.concat(left: binaries1, right: macos),
    binaries3 := str.concat(left: binaries2, right: release),
    binaryTitle := RenderHtmlTag(name: "h3", id: "", class: "", content: "Download Felidae Per OS"),
    binaries := str.concat(left: binaryTitle, right: RenderModuleGrid(content: binaries3)),
    vscode := DownloadCard(title: "VS Code Extension", badge: "VSX", text: "Install the Felidae VSIX for file icons, snippets, CodeLens, debugger integration, and Celidae-backed Problems.", href: "https://github.com/vishalkrishnaag/logicPrompts/releases", label: "Download Extension"),
    intellij := DownloadCard(title: "IntelliJ Plugin", badge: "IDE", text: "Install the JetBrains plugin archive for IntelliJ-based IDE support. Build from source when a release asset is not published.", href: "https://github.com/vishalkrishnaag/logicPrompts/releases", label: "Download Plugin"),
    extensionTitle := RenderHtmlTag(name: "h3", id: "", class: "", content: "Download Extensions"),
    extensions := str.concat(left: extensionTitle, right: RenderModuleGrid(content: str.concat(left: vscode, right: intellij))),
    guidance := RenderGuidance(doText: "Download Felidae and Celidae for the same version, then install the editor extension that points to those binaries.", dontText: "Do not mix a new extension with stale local binaries. Old diagnostics or old syntax support can make the editor disagree with the command line.", recommendText: "After installing on Linux, run ./build/celidae your_file.fx --check-json and ./build/felidae your_file.fx. If both work, the runtime, diagnostics, and editor path are aligned."),
    content1 := str.concat(left: guidance, right: binaries),
    content := str.concat(left: content1, right: extensions),
    return (HtmlRichSectionData(id: "downloads", title: "Download", p: "Download Felidae for execution and Celidae for diagnostics. Felidae runs programs, main() workflows, and the documentation server; Celidae powers checks, LSP, graph inspection, visualization, and editor feedback.", p2: "Use the GitHub Releases page as the generic binary path. Per-OS assets can be attached there as the project publishes builds. The editor extensions are listed separately so users can install runtime binaries and IDE support independently.", content: content, code: "Release page:\nhttps://github.com/vishalkrishnaag/logicPrompts/releases\n\nExpected assets:\nfelidae-windows-x64.zip\nfelidae-linux-x64.tar.gz\nfelidae-macos-arm64.tar.gz\nfelidae-vscode-0.0.2.vsix\nfelidae-intellij-plugin.zip", note: "When no release asset exists for your OS, build locally from source. Keep Felidae and Celidae installed together so execution, diagnostics, hosted docs, and editor integrations agree on syntax.", code2: "clang++ -std=c++17 -Wall -Wextra -Isrc -Ithird_party src/main.cpp src/FelidaeRuntime.cpp src/Visualization.cpp src/Lexer.cpp src/Parser.cpp src/Interpreter.cpp src/Env.cpp src/Memory.cpp native_modules/csv/NativeCsv.cpp native_modules/http/NativeHttp.cpp native_modules/process/NativeProcess.cpp -o build/felidae\n\nclang++ -std=c++17 -Wall -Wextra -Isrc -Ithird_party src/felidae_debug.cpp src/FelidaeRuntime.cpp src/Visualization.cpp src/Lexer.cpp src/Parser.cpp src/Interpreter.cpp src/Env.cpp src/Memory.cpp src/AstAnalyzer.cpp native_modules/csv/NativeCsv.cpp native_modules/http/NativeHttp.cpp native_modules/process/NativeProcess.cpp -o build/celidae\n\n./build/celidae docs/server.fx --check-json\n./build/felidae docs/server.fx\n\ncd vs-code-extension\nnpm install\nnpm run compile\nnpx vsce package")).
