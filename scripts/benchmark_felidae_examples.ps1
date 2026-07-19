param(
    [int] $Runs = 5
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$cases = @(
    @{ Name = "normal direct main"; Exe = "build\felidae.exe"; Args = @("examples\direct_main.fx", "one", "two") },
    @{ Name = "normal thread memory"; Exe = "build\felidae.exe"; Args = @("examples\thread_memory_test.fx") },
    @{ Name = "normal stdlib utilities"; Exe = "build\felidae.exe"; Args = @("examples\stdlib_utilities.fx") },
    @{ Name = "debug executes main"; Exe = "build\celidae.exe"; Args = @("examples\direct_main.fx", "one", "two") },
    @{ Name = "debug --check warnings"; Exe = "build\celidae.exe"; Args = @("examples\direct_main.fx", "--check-json") },
    @{ Name = "full felidae_test_suite.fx"; Exe = "build\felidae.exe"; Args = @("examples\felidae_test_suite.fx") }
)

function Invoke-BenchmarkCase {
    param(
        [hashtable] $Case
    )

    $times = @()
    $peaks = @()
    $exe = Join-Path $root $Case.Exe
    if (-not (Test-Path $exe)) {
        throw "Missing $exe. Run build.cmd first."
    }

    for ($i = 0; $i -lt $Runs; $i++) {
        $process = [System.Diagnostics.Process]::new()
        $process.StartInfo.FileName = $exe
        $process.StartInfo.WorkingDirectory = $root
        $process.StartInfo.Arguments = ($Case.Args | ForEach-Object {
            '"' + ($_ -replace '"', '\"') + '"'
        }) -join " "
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.RedirectStandardOutput = $true
        $process.StartInfo.RedirectStandardError = $true

        $timer = [System.Diagnostics.Stopwatch]::StartNew()
        [void] $process.Start()
        $maxWorkingSet = 0
        while (-not $process.HasExited) {
            $process.Refresh()
            if ($process.WorkingSet64 -gt $maxWorkingSet) {
                $maxWorkingSet = $process.WorkingSet64
            }
            Start-Sleep -Milliseconds 2
        }
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $timer.Stop()

        if ($process.ExitCode -ne 0) {
            throw "$($Case.Name) failed with exit code $($process.ExitCode): $stderr$stdout"
        }

        $times += $timer.Elapsed.TotalMilliseconds
        $peaks += ($maxWorkingSet / 1MB)
    }

    $sortedTimes = @($times | Sort-Object)
    $middle = [int]($sortedTimes.Count / 2)
    $median = if ($sortedTimes.Count % 2 -eq 0) {
        ($sortedTimes[$middle - 1] + $sortedTimes[$middle]) / 2
    } else {
        $sortedTimes[$middle]
    }

    [pscustomobject]@{
        Case = $Case.Name
        AvgMs = ($times | Measure-Object -Average).Average
        MedianMs = $median
        MinMs = ($times | Measure-Object -Minimum).Minimum
        MaxMs = ($times | Measure-Object -Maximum).Maximum
        PeakMb = ($peaks | Measure-Object -Maximum).Maximum
    }
}

$results = foreach ($case in $cases) {
    Invoke-BenchmarkCase -Case $case
}

"| Case | Avg | Median | Min | Max | Peak RAM |"
"|---|---:|---:|---:|---:|---:|"
foreach ($result in $results) {
    "| {0} | `{1:N2} ms` | `{2:N2} ms` | `{3:N2} ms` | `{4:N2} ms` | `{5:N2} MB max` |" -f `
        $result.Case, $result.AvgMs, $result.MedianMs, $result.MinMs, $result.MaxMs, $result.PeakMb
}
