param(
    [ValidateSet("native", "wasm")]
    [string] $Target = "native"
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path "build" | Out-Null

$commonSources = @(
    "src/FelidaeRuntime.cpp",
    "src/Visualization.cpp",
    "src/Lexer.cpp",
    "src/Parser.cpp",
    "src/Interpreter.cpp",
    "src/Env.cpp",
    "src/Memory.cpp",
    "native_modules/csv/NativeCsv.cpp",
    "native_modules/http/NativeHttp.cpp",
    "native_modules/process/NativeProcess.cpp"
)

$debugSources = $commonSources + @("src/AstAnalyzer.cpp")

$extraLibs = @()
if ($env:FELIDAE_DLOPEN_LIBS) {
    $extraLibs = $env:FELIDAE_DLOPEN_LIBS -split "\s+"
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
    & clang++ -std=c++17 -Wall -Wextra -Isrc -Ithird_party @Sources -o $Output @extraLibs
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
Invoke-FelidaeBuild -Output "build/felidae.exe" -Sources (@("src/main.cpp") + $commonSources)
Invoke-FelidaeBuild -Output "build/celidae.exe" -Sources (@("src/felidae_debug.cpp") + $debugSources)
Invoke-FelidaeBuild -Output "build/felidae_debug.exe" -Sources (@("src/felidae_debug.cpp") + $debugSources)

Write-Host "Built build/felidae.exe, build/celidae.exe, and build/felidae_debug.exe"
