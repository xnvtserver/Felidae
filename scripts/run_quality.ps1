param(
    [ValidateSet("debug", "release", "production", "sanitize")]
    [string] $Configuration = "debug",
    [switch] $SkipBuild,
    [switch] $RunFullExamples,
    [switch] $Strict
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$qualityDir = Join-Path $root "build\quality"
$report = Join-Path $qualityDir "report.md"
New-Item -ItemType Directory -Force -Path $qualityDir | Out-Null

function Add-Report {
    param([string] $Text)
    Add-Content -Path $report -Value $Text
}

function Test-Command {
    param([string] $Name)
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Invoke-Logged {
    param(
        [string] $Title,
        [scriptblock] $Command,
        [bool] $AllowFailure = $false
    )

    Add-Report ""
    Add-Report "## $Title"
    Add-Report '```text'
    try {
        $output = & $Command 2>&1
        if ($output) { Add-Report ($output | Out-String).TrimEnd() }
        Add-Report '```'
        return $true
    } catch {
        Add-Report ($_ | Out-String).TrimEnd()
        Add-Report '```'
        if (-not $AllowFailure -and $Strict) { throw }
        return $false
    }
}

function Invoke-CmdLogged {
    param(
        [string] $Title,
        [string] $Command,
        [bool] $AllowFailure = $false
    )

    Add-Report ""
    Add-Report "## $Title"
    Add-Report '```text'
    $output = & cmd /c "$Command 2>&1"
    if ($output) { Add-Report ($output | Out-String).TrimEnd() }
    Add-Report '```'
    if ($LASTEXITCODE -ne 0 -and -not $AllowFailure -and $Strict) {
        throw "$Title failed with exit code $LASTEXITCODE"
    }
    return $LASTEXITCODE -eq 0
}

Set-Content -Path $report -Value "# Felidae Quality Report`n`nGenerated: $(Get-Date -Format o)`nConfiguration: $Configuration"

if (-not $SkipBuild) {
    Invoke-Logged -Title "Build" -Command {
        & (Join-Path $root "build.cmd") "native" "--configuration" $Configuration
    } | Out-Null
}

$tidyFiles = @(
    "src\Interpreter.cpp",
    "src\Env.cpp",
    "src\Memory.cpp",
    "src\NativeRuntime.cpp",
    "src\BuiltinRegistry.cpp",
    "src\FelidaeRuntime.cpp"
)

if (Test-Command "clang-tidy") {
    foreach ($file in $tidyFiles) {
        Invoke-CmdLogged -Title "clang-tidy $file" -AllowFailure $true -Command "clang-tidy `"$file`" -- -std=c++17 -Isrc -isystem third_party" | Out-Null
    }
} else {
    Add-Report "`n## clang-tidy`nSkipped: clang-tidy was not found on PATH."
}

if (Test-Command "cppcheck") {
    Invoke-CmdLogged -Title "cppcheck" -AllowFailure $true -Command "cppcheck --enable=warning,performance,portability,style --std=c++17 --inline-suppr --suppress=missingIncludeSystem -Isrc -Ithird_party src native_modules" | Out-Null
} else {
    Add-Report "`n## cppcheck`nSkipped: cppcheck was not found on PATH. Install cppcheck and rerun this script."
}

if (Test-Command "CodeChecker") {
    Invoke-Logged -Title "CodeChecker analyze" -AllowFailure $true -Command {
        $log = Join-Path $qualityDir "compile_commands.json"
        & CodeChecker log -b "build.cmd native --configuration $Configuration" -o $log
        & CodeChecker analyze $log -o (Join-Path $qualityDir "codechecker")
    } | Out-Null
} else {
    Add-Report "`n## CodeChecker`nSkipped: CodeChecker was not found on PATH. Install CodeChecker and rerun this script."
}

Add-Report "`n## Valgrind"
Add-Report "Skipped: Valgrind is not available for native Windows executables. Run scripts/run_quality.sh on Linux or WSL for memcheck."

if ($RunFullExamples) {
    Invoke-Logged -Title "Felidae example regression" -Command {
        & powershell -ExecutionPolicy Bypass -File (Join-Path $root "scripts\test_felidae_examples.ps1")
    } | Out-Null
} else {
    Invoke-Logged -Title "Felidae smoke .fx programs" -Command {
        & (Join-Path $root "build\felidae.exe") "examples\then_pipeline.fx"
        & (Join-Path $root "build\felidae.exe") "examples\native_thread_smoke.fx"
        & (Join-Path $root "build\felidae.exe") "examples\direct_main.fx" "one" "two"
        & (Join-Path $root "build\celidae.exe") "docs\server.fx" "--check-json"
    } | Out-Null
}

Write-Host "Quality report written to $report"
