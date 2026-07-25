param(
    [int] $Runs = 5,
    [switch] $SkipBuild
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exe = Join-Path $root "build\felidae.exe"

if (-not $SkipBuild) {
    & (Join-Path $root "build.ps1") -Configuration production
    if ($LASTEXITCODE -ne 0) {
        throw "Production build failed."
    }
}
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Missing $exe. Run .\build.cmd --configuration production first."
}

$cases = @(
    @{ Name = "direct main"; Args = @("examples\direct_main.fx", "one", "two") },
    @{ Name = "recursive backtracking"; Args = @("examples\recursive_ancestor.fx", "? AncestorOf(descendant: descendant, ancestor: ancestor)", "--benchmark-repeat", "100") },
    @{ Name = "indexed fact property"; Args = @("examples\data\converted_csv_country.fx", '? Country(name: "India", alpha_2: alpha2)', "--benchmark-repeat", "100") },
    @{ Name = "full fact scan"; Args = @("examples\data\converted_csv_country.fx", "? Country(name: name)", "--benchmark-repeat", "20") },
    @{ Name = "thread snapshot"; Args = @("examples\thread_snapshot_test.fx") },
    @{ Name = "stdlib utilities"; Args = @("examples\stdlib_utilities.fx") },
    @{ Name = "fact reasoning workload"; Args = @("examples\advanced_mortality_fact_reasoning.fx") }
)

function Invoke-FelidaeProcess {
    param(
        [string[]] $Arguments,
        [int] $TimeoutSeconds = 60
    )

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo.FileName = $exe
    $process.StartInfo.WorkingDirectory = $root
    $process.StartInfo.Arguments = (($Arguments + "--metrics-json") | ForEach-Object {
        '"' + ($_ -replace '"', '\"') + '"'
    }) -join " "
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true

    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    [void] $process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $peakWorkingSet = 0
    while (-not $process.HasExited) {
        $process.Refresh()
        if ($process.WorkingSet64 -gt $peakWorkingSet) {
            $peakWorkingSet = $process.WorkingSet64
        }
        if ($timer.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
            try {
                $process.Kill()
            } catch {
                # The process may have exited between the timeout check and Kill().
            }
            $process.WaitForExit()
            throw "Process exceeded the $TimeoutSeconds second timeout: $($Arguments -join ' ')"
        }
        Start-Sleep -Milliseconds 2
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $timer.Stop()

    if ($process.ExitCode -ne 0) {
        throw "Process failed with exit code $($process.ExitCode): $stderr$stdout"
    }
    $metricMatch = [regex]::Match($stderr, 'FELIDAE_METRICS (\{.*\})')
    if (-not $metricMatch.Success) {
        throw "Interpreter did not emit FELIDAE_METRICS for: $($Arguments -join ' ')"
    }

    [pscustomobject]@{
        TotalMs = $timer.Elapsed.TotalMilliseconds
        PeakMb = $peakWorkingSet / 1MB
        Metrics = $metricMatch.Groups[1].Value | ConvertFrom-Json
    }
}

function Invoke-BenchmarkCase {
    param([hashtable] $Case)

    Write-Host ("Benchmarking {0} (warm-up + {1} run(s))..." -f $Case.Name, $Runs)

    # Validate and warm filesystem/runtime dependencies before measured runs.
    [void] (Invoke-FelidaeProcess -Arguments $Case.Args)

    $samples = for ($i = 0; $i -lt $Runs; $i++) {
        Write-Progress `
            -Activity "Felidae benchmark" `
            -Status ("{0}: run {1}/{2}" -f $Case.Name, ($i + 1), $Runs) `
            -PercentComplete ((($i + 1) / $Runs) * 100)
        Invoke-FelidaeProcess -Arguments $Case.Args
    }
    Write-Progress -Activity "Felidae benchmark" -Completed
    $times = @($samples | ForEach-Object { $_.TotalMs } | Sort-Object)
    $middle = [int]($times.Count / 2)
    $median = if ($times.Count % 2 -eq 0) {
        ($times[$middle - 1] + $times[$middle]) / 2
    } else {
        $times[$middle]
    }

    [pscustomobject]@{
        Case = $Case.Name
        AvgMs = ($samples.TotalMs | Measure-Object -Average).Average
        MedianMs = $median
        LoadMs = ($samples.Metrics.loadMs | Measure-Object -Average).Average
        ExecuteMs = ($samples.Metrics.executionMs | Measure-Object -Average).Average
        PeakMb = ($samples.PeakMb | Measure-Object -Maximum).Maximum
        EnvCopies = ($samples.Metrics.runtime.environmentCopies | Measure-Object -Average).Average
        Unifications = ($samples.Metrics.runtime.unificationAttempts | Measure-Object -Average).Average
        FactCandidates = ($samples.Metrics.runtime.factCandidates | Measure-Object -Average).Average
        EnvFrames = ($samples.Metrics.runtime.environmentFramesCreated | Measure-Object -Average).Average
        StandardizedClauses = ($samples.Metrics.runtime.standardizedClauses | Measure-Object -Average).Average
        FirstQueryMs = ($samples.Metrics.firstQueryMs | Measure-Object -Average).Average
        RepeatedQueryMs = ($samples.Metrics.repeatedQueryAverageMs | Measure-Object -Average).Average
    }
}

$results = foreach ($case in $cases) {
    Invoke-BenchmarkCase -Case $case
}

"| Case | Total median | Load | Execute | First query | Repeated query | Peak RAM | Env frames | Env copies | Standardized clauses | Unifications | Fact candidates |"
"|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
foreach ($result in $results) {
    "| {0} | `{1:N2} ms` | `{2:N2} ms` | `{3:N2} ms` | `{4:N3} ms` | `{5:N3} ms` | `{6:N2} MB` | `{7:N0}` | `{8:N0}` | `{9:N0}` | `{10:N0}` | `{11:N0}` |" -f `
        $result.Case,
        $result.MedianMs,
        $result.LoadMs,
        $result.ExecuteMs,
        $result.FirstQueryMs,
        $result.RepeatedQueryMs,
        $result.PeakMb,
        $result.EnvFrames,
        $result.EnvCopies,
        $result.StandardizedClauses,
        $result.Unifications,
        $result.FactCandidates
}
