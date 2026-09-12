param(
    [ValidateSet("native", "windows-x64")]
    [string] $Target = "native",
    [ValidateSet("debug", "release", "sanitize")]
    [string] $Configuration = "release",
    [switch] $Test
)

$ErrorActionPreference = "Stop"
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildType = if ($Configuration -eq "debug" -or $Configuration -eq "sanitize") { "Debug" } else { "Release" }
$buildDir = Join-Path $root "build\windows-x64\$($buildType.ToLowerInvariant())"
$sanitizers = if ($Configuration -eq "sanitize") { "ON" } else { "OFF" }

& $cmake -S $root -B $buildDir "-DCMAKE_BUILD_TYPE=$buildType" "-DFELIDAE_BUILD_TESTS=$($Test.IsPresent)" "-DFELIDAE_ENABLE_SANITIZERS=$sanitizers"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& $cmake --build $buildDir --config $buildType --target felidae --parallel 1
if ($LASTEXITCODE -ne 0) { throw "Felidae interpreter build failed" }
if ($Test) {
    & ctest --test-dir $buildDir --build-config $buildType --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "CTest failed" }
}
