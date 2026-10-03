param(
    [ValidateSet("debug", "release", "sanitize")]
    [string] $Configuration = "debug",
    [ValidateSet("x64", "x86", "arm", "arm64")]
    [string] $Platform = "x64",
    [ValidateRange(1, 256)]
    [int] $Jobs = 1,
    [switch] $Test
)

$ErrorActionPreference = "Stop"
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildType = if ($Configuration -eq "debug" -or $Configuration -eq "sanitize") { "Debug" } else { "Release" }
$buildName = if ($Configuration -eq "sanitize") { "asan" } else { $Configuration }
$buildDir = Join-Path $root "build\$buildName\$Platform"
$sanitizers = if ($Configuration -eq "sanitize") { "ON" } else { "OFF" }

$configureArgs = @(
    "-S", $root,
    "-B", $buildDir,
    "-DCMAKE_BUILD_TYPE=$buildType",
    "-DFELIDAE_BUILD_TESTS=$($Test.IsPresent)",
    "-DFELIDAE_ENABLE_SANITIZERS=$sanitizers",
    "-DFELIDAE_DEPENDENCY_JOBS=$Jobs"
)

# VsDevCmd selects the compiler environment, but Visual Studio's CMake
# generator still needs an explicit generator platform. User-facing names
# remain stable while this table contains Visual Studio's spellings.
if ($env:OS -eq "Windows_NT" -and
    ([string]::IsNullOrWhiteSpace($env:CMAKE_GENERATOR) -or
     $env:CMAKE_GENERATOR -like "Visual Studio*")) {
    $cmakePlatform = switch ($Platform) {
        "x64" { "x64" }
        "x86" { "Win32" }
        "arm" { "ARM" }
        "arm64" { "ARM64" }
    }
    $cache = Join-Path $buildDir "CMakeCache.txt"
    if (Test-Path -LiteralPath $cache) {
        $cachedPlatform = Select-String -LiteralPath $cache `
            -Pattern '^CMAKE_GENERATOR_PLATFORM:INTERNAL=(.*)$' |
            Select-Object -First 1
        if ($cachedPlatform -and
            $cachedPlatform.Matches[0].Groups[1].Value -ne $cmakePlatform) {
            throw "Build tree '$buildDir' uses platform '$($cachedPlatform.Matches[0].Groups[1].Value)', not '$cmakePlatform'. Remove that generated directory and rerun this command."
        }
    }
    $configureArgs += @("-A", $cmakePlatform)
}

& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
$targets = @("felidae")
if ($Test) { $targets += "felidae_storage_tests" }
& $cmake --build $buildDir --config $buildType --target $targets --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "Felidae interpreter build failed" }
if ($Test) {
    & ctest --test-dir $buildDir --build-config $buildType --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "CTest failed" }
}
