param(
    [ValidateSet("native", "windows-x64", "windows-arm64", "wasm")]
    [string] $Target = "native",
    [ValidateSet("debug", "release", "production", "sanitize")]
    [string] $Configuration = "debug",
    [switch] $WarningsAsErrors
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path "build" | Out-Null

$commonSources = @(
    "src/FelidaeRuntime.cpp",
    "src/Visualization.cpp",
    "src/BuiltinRegistry.cpp",
    "src/Lexer.cpp",
    "src/Parser.cpp",
    "src/Interpreter.cpp",
    "src/Env.cpp",
    "src/Memory.cpp",
    "src/NativeRuntime.cpp",
    "native_modules/csv/NativeCsv.cpp",
    "native_modules/http/NativeHttp.cpp",
    "native_modules/process/NativeProcess.cpp"
)

$debugSources = $commonSources + @("src/AstAnalyzer.cpp")

$extraLibs = @()
if ($env:FELIDAE_DLOPEN_LIBS) {
    $extraLibs = $env:FELIDAE_DLOPEN_LIBS -split "\s+"
}

$warningFlags = @("-Wall", "-Wextra", "-Wpedantic", "-Wshadow", "-Wnon-virtual-dtor", "-Wold-style-cast", "-Woverloaded-virtual")
if ($WarningsAsErrors) {
    $warningFlags += "-Werror"
}

$configFlags = switch ($Configuration) {
    "debug" { @("-O0", "-g") }
    "release" { @("-O2", "-DNDEBUG") }
    "production" { @("-O3", "-DNDEBUG", "-flto", "-fuse-ld=lld") }
    "sanitize" { @("-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer") }
}

$targetFlags = @()
if ($Target -eq "windows-x64") {
    $targetFlags = @("--target=x86_64-pc-windows-msvc")
} elseif ($Target -eq "windows-arm64") {
    $targetFlags = @("--target=arm64-pc-windows-msvc")
}

function Assert-Command {
    param(
        [string] $Name,
        [string] $InstallMessage
    )

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        [Console]::Error.WriteLine($InstallMessage)
        exit 1
    }
}

function Invoke-FelidaeBuild {
    param(
        [string] $Output,
        [string[]] $Sources
    )

    Write-Host "Building $Output"
    & clang++ -std=c++17 @warningFlags @configFlags @targetFlags -Isrc -isystem third_party @Sources -o $Output @extraLibs
    if ($LASTEXITCODE -ne 0) {
        throw "clang++ failed while building $Output"
    }
}

if ($Target -eq "wasm") {
    Assert-Command -Name "em++" -InstallMessage "WASM build requires Emscripten. Install and activate emsdk so em++ is on PATH, then run .\build.cmd wasm."
    New-Item -ItemType Directory -Force -Path "docs/wasm" | Out-Null
    Write-Host "Building docs/wasm/felidae_wasm.js"
    & em++ -std=c++17 -O2 -fexceptions -Isrc -Ithird_party `
        src/felidae_wasm.cpp @commonSources `
        --bind `
        -s MODULARIZE=1 `
        -s "EXPORT_NAME='FelidaeWasm'" `
        -s ENVIRONMENT=web `
        -s ALLOW_MEMORY_GROWTH=1 `
        -s DISABLE_EXCEPTION_CATCHING=0 `
        -s ASSERTIONS=1 `
        --preload-file core@/core `
        -o docs/wasm/felidae_wasm.js
    if ($LASTEXITCODE -ne 0) {
        throw "em++ failed while building docs/wasm/felidae_wasm.js"
    }
    Write-Host "Built docs/wasm/felidae_wasm.js, docs/wasm/felidae_wasm.wasm, and docs/wasm/felidae_wasm.data"
    exit 0
}

Assert-Command -Name "clang++" -InstallMessage "Native build requires clang++. Install LLVM/Clang and make sure clang++ is on PATH."
$suffix = ""
if ($Target -eq "windows-x64") { $suffix = "-windows-x64" }
if ($Target -eq "windows-arm64") { $suffix = "-windows-arm64" }

Invoke-FelidaeBuild -Output "build/felidae$suffix.exe" -Sources (@("src/main.cpp") + $commonSources)
Invoke-FelidaeBuild -Output "build/celidae$suffix.exe" -Sources (@("src/felidae_debug.cpp") + $debugSources)
Invoke-FelidaeBuild -Output "build/felidae_debug$suffix.exe" -Sources (@("src/felidae_debug.cpp") + $debugSources)

Write-Host "Built build/felidae$suffix.exe, build/celidae$suffix.exe, and build/felidae_debug$suffix.exe"
