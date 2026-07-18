import ("html_components.fx", "str").

DownloadReleaseUrl() =>
    return ("https://github.com/xnvtserver/Felidae/releases").

DownloadRepoUrl() =>
    return ("https://github.com/xnvtserver/Felidae").

DownloadCard(title: string, badge: string, text: string, href: string, label: string) =>
    icon := RenderHtmlTag(name: "span", id: "", class: "download-mark", content: badge),
    intro := RenderParagraph(text: text),
    action := RenderAnchor(input: HtmlAnchorData(label: label, href: href, class: "tool-button")),
    body1 := str.concat(left: icon, right: intro),
    body2 := str.concat(left: body1, right: action),
    return (RenderMiniCardContent(input: HtmlCardContentData(title: title, content: body2))).

DocsDownloadPage() =>
    releases := DownloadReleaseUrl(),
    repo := DownloadRepoUrl(),
    windows := DownloadCard(title: "Windows", badge: "WIN", text: "Use the Windows x64 zip with felidae.exe, celidae.exe, and felidae_debug.exe. Local source builds use build.cmd or build.ps1 with LLVM clang++.", href: releases, label: "Get Windows Zip"),
    debian := DownloadCard(title: "Debian And Ubuntu", badge: "DEB", text: "Use the .deb package when published. Source builds use ./build.sh and can install clang and make with apt after user approval.", href: releases, label: "Get .deb"),
    fedora := DownloadCard(title: "Fedora", badge: "RPM", text: "Use the RPM package when published. Source builds can install clang, make, and gcc-c++ through dnf after approval.", href: releases, label: "Get RPM"),
    opensuse := DownloadCard(title: "openSUSE", badge: "SUSE", text: "Use the RPM package when published. Source builds can install clang and gcc-c++ through zypper after approval.", href: releases, label: "Get RPM"),
    arch := DownloadCard(title: "Arch And Manjaro", badge: "ARC", text: "Use the pkg.tar.zst package when published. Source builds can install clang, make, and base-devel through pacman after approval.", href: releases, label: "Get Arch Package"),
    linuxTar := DownloadCard(title: "Generic Linux", badge: "TAR", text: "Use felidae-linux-x64.tar.gz when distro packages are not available. It should include Felidae, Celidae, docs assets, and checksums.", href: releases, label: "Get Tarball"),
    macos := DownloadCard(title: "macOS Intel And Apple Silicon", badge: "MAC", text: "Use macOS release archives for x64 or arm64 when available. Source builds use Apple Command Line Tools or Homebrew LLVM.", href: releases, label: "Get macOS Archive"),
    android := DownloadCard(title: "Android", badge: "AND", text: "Android is an NDK cross-build target. Configure ANDROID_NDK_HOME and ANDROID_ABI; it is not a normal desktop installer.", href: releases, label: "Get Android Assets"),
    wasm := DownloadCard(title: "Browser WASM", badge: "WASM", text: "Build or download the WebAssembly runtime for the docs playground. It serves docs/wasm/felidae_wasm.js, .wasm, and .data.", href: releases, label: "Get WASM Runtime"),
    source := DownloadCard(title: "Source Repository", badge: "Fx", text: "The canonical project repository is xnvtserver/Felidae. Use it for source, issues, releases, packaging, editor downloads, and checksums.", href: repo, label: "Open Repository"),
    cards := str.join(data: [windows, debian, fedora, opensuse, arch, linuxTar, macos, android, wasm, source], delimiter: ""),
    binaryTitle := RenderHtmlTag(name: "h3", id: "", class: "", content: "Download Felidae Per OS"),
    binaries := str.concat(left: binaryTitle, right: RenderModuleGrid(content: cards)),
    vscode := DownloadCard(title: "VS Code Extension", badge: "VSX", text: "Install the Felidae VSIX matching the same release as your Felidae and Celidae binaries.", href: releases, label: "Download Extension"),
    intellij := DownloadCard(title: "IntelliJ Plugin", badge: "IDE", text: "Install the JetBrains plugin archive matching your runtime release, or build the plugin from source.", href: releases, label: "Download Plugin"),
    extensionTitle := RenderHtmlTag(name: "h3", id: "", class: "", content: "Download Extensions"),
    extensions := str.concat(left: extensionTitle, right: RenderModuleGrid(content: str.concat(left: vscode, right: intellij))),
    guidance := RenderGuidance(doText: "Install Felidae and Celidae from the same release, then point editor extensions at those binaries.", dontText: "Do not mix an old Celidae diagnostics binary with a newer editor extension. Syntax checks and runtime behavior can drift.", recommendText: "For Linux source builds, run ./build.sh first, then scripts/package-linux.sh to create distro packaging from the built binaries."),
    content1 := str.concat(left: guidance, right: binaries),
    content := str.concat(left: content1, right: extensions),
    return (HtmlRichSectionData(id: "download", title: "Download Felidae", p: "This page is separate from the language documentation. Use it to get Felidae for execution, Celidae for diagnostics, editor extensions, source packages, and browser WASM assets.", p2: "Release packaging is OS-specific. Supported source-build targets include Windows, Debian, Ubuntu, Fedora, openSUSE, Arch, Manjaro, macOS, Android NDK, and browser WASM. Linux users should prefer their distro package when available and use the generic tarball as a fallback.", content: content, code: "Release page:\nhttps://github.com/xnvtserver/Felidae/releases\n\nExpected assets:\nfelidae-windows-x64.zip\nfelidae-debian-ubuntu-amd64.deb\nfelidae-fedora-x86_64.rpm\nfelidae-opensuse-x86_64.rpm\nfelidae-arch-manjaro-x86_64.pkg.tar.zst\nfelidae-linux-x64.tar.gz\nfelidae-macos-x64.tar.gz\nfelidae-macos-arm64.tar.gz\nfelidae-android-arm64-v8a.tar.gz\nfelidae-wasm.zip\nfelidae-vscode-0.0.2.vsix\nfelidae-intellij-plugin.zip", note: "When no release asset exists for your OS, build locally from source. Keep Felidae and Celidae installed together so execution, diagnostics, hosted docs, WASM playground behavior, and editor integrations agree on syntax.", code2: "# Native Linux/macOS build:\n./build.sh\n\n# Linux package artifacts after native build:\nscripts/package-linux.sh\n\n# Windows build:\n.\\build.cmd\n\n# Android cross-build:\nANDROID_NDK_HOME=/opt/android-ndk ANDROID_ABI=arm64-v8a ./build.sh --target android\n\n# Browser playground runtime on Windows:\n.\\build.cmd wasm\n\n# Browser playground runtime on Linux/macOS:\n./build.sh --target wasm\n\n# Verify after install:\nfelidae examples/main.fx\ncelidae examples/main.fx --check-json")).
