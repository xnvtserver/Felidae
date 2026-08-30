param(
    [ValidateSet("native", "windows-x64")]
    [string] $Target = "native",
    [ValidateSet("debug", "release", "production", "sanitize")]
    [string] $Configuration = "release",
    [switch] $WarningsAsErrors,
    [switch] $Test,
    [switch] $Dist,
    [switch] $Beta
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
if ($Target -notin @("native", "windows-x64")) {
    throw "Unsupported target '$Target': provide a CMake toolchain/preset for SentencePiece cross-compilation."
}
if (-not $env:FELIDAE_LIBTORCH_PATH) {
    throw "Windows builds require FELIDAE_LIBTORCH_PATH to name the provisioned CPU LibTorch installation."
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildType = if ($Configuration -eq "debug" -or $Configuration -eq "sanitize") { "Debug" } else { "Release" }
$buildDir = if ($env:FELIDAE_BUILD_DIR) { $env:FELIDAE_BUILD_DIR } else { Join-Path $root "build\windows-x64\$($buildType.ToLowerInvariant())" }
$configureArgs = @(
    "-S", $root,
    "-B", $buildDir,
    "-DCMAKE_BUILD_TYPE=$buildType"
)
if ($Test) {
    $configureArgs += "-DFELIDAE_BUILD_TESTS=ON"
}
if ($WarningsAsErrors) {
    $configureArgs += "-DFELIDAE_ENABLE_STRICT_WARNINGS=ON"
}
if ($env:FELIDAE_LIBTORCH_PATH) {
    $configureArgs += "-DCMAKE_PREFIX_PATH=$env:FELIDAE_LIBTORCH_PATH"
}
if ($env:FELIDAE_VERSION) {
    $configureArgs += "-DFELIDAE_VERSION=$env:FELIDAE_VERSION"
}
if ($Configuration -eq "sanitize") {
    $configureArgs += "-DFELIDAE_ENABLE_SANITIZERS=ON"
}

& $cmakePath @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

$jobs = if ($env:FELIDAE_JOBS) { $env:FELIDAE_JOBS } else { "1" }
$targets = if ($Beta) { @("felidae_beta") } elseif ($Dist) { @("felidae_dist") } elseif ($Test) { @("felidae_compiler", "felidae_vm", "felidae_debugger", "felidae_tests") } else { @("felidae_compiler", "felidae_vm", "felidae_debugger") }
& $cmakePath --build $buildDir --config $buildType --target $targets --parallel $jobs
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }
if ($Test) {
    & ctest --test-dir $buildDir --build-config $buildType --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "CTest failed" }
}

if ($Dist -or $Beta) {
    foreach ($executable in @("felidae_compiler.exe", "felidae_vm.exe", "felidae_debugger.exe")) {
        $path = Join-Path $buildDir "dist\bin\$executable"
        if (-not (Test-Path $path)) {
            throw "Distribution staging completed without expected executable: $path"
        }
    }
}
if (($Dist -or $Beta) -and -not (Test-Path (Join-Path $buildDir "dist\models\felidae.model"))) {
    throw "Distribution staging completed without dist\models\felidae.model"
}
