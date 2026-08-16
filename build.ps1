param(
    [ValidateSet("native", "windows-x64", "windows-arm64", "wasm")]
    [string] $Target = "native",
    [ValidateSet("debug", "release", "production", "sanitize")]
    [string] $Configuration = "release",
    [switch] $WarningsAsErrors
)

$ErrorActionPreference = "Stop"

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmakePath -and $env:VSINSTALLDIR) {
    $candidate = Join-Path $env:VSINSTALLDIR "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $candidate) { $cmakePath = $candidate }
}
if (-not $cmakePath) {
    foreach ($version in "18", "17") {
        foreach ($edition in "Community", "Professional", "Enterprise", "BuildTools") {
            $candidate = Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\$version\$edition\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $candidate) { $cmakePath = $candidate; break }
        }
        if ($cmakePath) { break }
    }
}
if (-not $cmakePath) { throw "CMake was not found. Install Visual Studio C++ CMake tools or add cmake to PATH." }

# CMake is the only supported native build graph.  It owns SentencePiece and
# the generated TokenId vocabulary, avoiding a stale direct compiler list.
if ($Target -ne "native") {
    throw "Unsupported target '$Target': provide a CMake toolchain/preset for SentencePiece cross-compilation."
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = if ($env:FELIDAE_BUILD_DIR) { $env:FELIDAE_BUILD_DIR } else { Join-Path $root "build" }
$buildType = if ($Configuration -eq "debug" -or $Configuration -eq "sanitize") { "Debug" } else { "Release" }
$configureArgs = @(
    "-S", $root,
    "-B", $buildDir,
    "-DCMAKE_BUILD_TYPE=$buildType"
)
if ($Configuration -eq "sanitize") {
    $configureArgs += "-DFELIDAE_ENABLE_SANITIZERS=ON"
}

& $cmakePath @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

$jobs = if ($env:FELIDAE_JOBS) { $env:FELIDAE_JOBS } else { "1" }
& $cmakePath --build $buildDir --config $buildType --target felidae_compiler felidae_vm --parallel $jobs
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

foreach ($executable in @("felidae_compiler.exe", "felidae_vm.exe")) {
    $path = Join-Path $buildDir $executable
    if ($IsWindows -and -not (Test-Path $path)) {
        throw "Build completed but expected executable was not created: $path"
    }
}
