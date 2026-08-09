param(
    [ValidateSet("native", "windows-x64", "windows-arm64", "wasm")]
    [string] $Target = "native",
    [ValidateSet("debug", "release", "production", "sanitize")]
    [string] $Configuration = "release",
    [switch] $WarningsAsErrors
)

$ErrorActionPreference = "Stop"

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

$localAbseil = Join-Path $root "build-sentencepiece/_deps/abseil-cpp-src"
$localProtobuf = Join-Path $root "build-sentencepiece/_deps/protobuf-src"
if (Test-Path $localAbseil) {
    $configureArgs += "-DFETCHCONTENT_SOURCE_DIR_ABSEIL-CPP=$localAbseil"
}
if (Test-Path $localProtobuf) {
    $configureArgs += "-DFETCHCONTENT_SOURCE_DIR_PROTOBUF=$localProtobuf"
}

& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

$jobs = if ($env:FELIDAE_JOBS) { $env:FELIDAE_JOBS } else { "1" }
& cmake --build $buildDir --parallel $jobs
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }
