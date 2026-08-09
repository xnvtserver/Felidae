param(
    [int] $Runs = 5,
    [switch] $SkipBuild,
    [string] $Exe = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exe = if ($Exe) { $Exe } else { Join-Path $root "build\felidae.exe" }

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
    @{ Name = "direct main"; Args = @("examples\direct_main.fx", "one", "two"); Expect = @('count: 3', 'args: ["one", "two"]') },
    @{ Name = "recursive system.run"; Args = @("v2_examples\benchmark_recursive_system_run.fx", "--benchmark-repeat", "100"); Expect = @('QueryResult(', 'Ancestor: "organism"', 'count: 3') },
    @{ Name = "indexed system.run"; Args = @("v2_examples\selective_fact_query.fx", "--benchmark-repeat", "100"); Expect = @('QuerySolution(', 'Id: "c01"', 'count: 1') },
    # External query mode remains a compatibility benchmark; system.run fixtures are primary.
    @{ Name = "external query compatibility"; Args = @("examples\data\converted_csv_country.fx", '? Country(name: "India", alpha_2: alpha2)', "--benchmark-repeat", "100"); Expect = @('alpha2 = "IN"') },
    @{ Name = "thread snapshot"; Args = @("examples\thread_snapshot_test.fx"); Expect = @('started: "started"', 'status: "finished"') },
    @{ Name = "stdlib utilities"; Args = @("examples\stdlib_utilities.fx"); Expect = @('has_engineer: true', 'write_status: "ok"', 'line_count: 3') },
    @{ Name = "fact reasoning workload"; Args = @("examples\advanced_mortality_fact_reasoning.fx"); Expect = @('Mortal') },
    # Search microbenchmarks use enough in-process repetitions to dominate process-launch jitter.
    @{ Name = "linear search"; Args = @("v2_examples\standard_search_algorithms.fx", "? LinearSearchLast() == 63", "--benchmark-repeat", "10000"); Expect = @('true') },
    @{ Name = "binary search"; Args = @("v2_examples\standard_search_algorithms.fx", "? BinarySearchLast() == 63", "--benchmark-repeat", "10000"); Expect = @('true') }
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
        Output = $stdout
        Metrics = $metricMatch.Groups[1].Value | ConvertFrom-Json
    }
}

function Invoke-BenchmarkCase {
    param([hashtable] $Case)

    Write-Host ("Benchmarking {0} (warm-up + {1} run(s))..." -f $Case.Name, $Runs)

    # Validate and warm filesystem/runtime dependencies before measured runs.
    $warmup = Invoke-FelidaeProcess -Arguments $Case.Args
    foreach ($expected in @($Case.Expect)) {
        if (-not $warmup.Output.Contains($expected)) {
            throw "Benchmark fixture '$($Case.Name)' did not produce expected output '$expected'. Actual output: $($warmup.Output)"
        }
    }

    $samples = for ($i = 0; $i -lt $Runs; $i++) {
        Write-Progress `
            -Activity "Felidae benchmark" `
            -Status ("{0}: run {1}/{2}" -f $Case.Name, ($i + 1), $Runs) `
            -PercentComplete ((($i + 1) / $Runs) * 100)
        Invoke-FelidaeProcess -Arguments $Case.Args
    }
    Write-Progress -Activity "Felidae benchmark" -Completed
    function Get-Median([double[]]$Values) {
        $ordered = @($Values | Sort-Object)
        $middle = [int]($ordered.Count / 2)
        if ($ordered.Count % 2 -eq 0) {
            return ($ordered[$middle - 1] + $ordered[$middle]) / 2
        }
        return $ordered[$middle]
    }
    $times = @($samples | ForEach-Object { $_.TotalMs })
    $median = Get-Median $times
    $average = ($times | Measure-Object -Average).Average
    $sumSquares = 0.0
    foreach ($time in $times) { $sumSquares += [Math]::Pow($time - $average, 2) }
    $standardDeviation = [Math]::Sqrt($sumSquares / $times.Count)

    [pscustomobject]@{
        Case = $Case.Name
        AvgMs = $average
        MedianMs = $median
        TotalCv = if ($average -gt 0) { 100.0 * $standardDeviation / $average } else { 0.0 }
        LoadMs = Get-Median @($samples.Metrics.loadMs)
        ExecuteMs = Get-Median @($samples.Metrics.executionMs)
        PeakMb = ($samples.PeakMb | Measure-Object -Maximum).Maximum
        EnvCopies = ($samples.Metrics.runtime.environmentCopies | Measure-Object -Average).Average
        ClauseAttempts = ($samples.Metrics.runtime.clauseAttempts | Measure-Object -Average).Average
        Unifications = ($samples.Metrics.runtime.unificationAttempts | Measure-Object -Average).Average
        FactCandidates = ($samples.Metrics.runtime.factCandidates | Measure-Object -Average).Average
        RelationshipCandidates = ($samples.Metrics.runtime.relationshipCandidates | Measure-Object -Average).Average
        RelationshipPruned = ($samples.Metrics.runtime.relationshipCandidatesPruned | Measure-Object -Average).Average
        EnvFrames = ($samples.Metrics.runtime.environmentFramesCreated | Measure-Object -Average).Average
        StandardizedClauses = ($samples.Metrics.runtime.standardizedClauses | Measure-Object -Average).Average
        FirstQueryMs = Get-Median @($samples.Metrics.firstQueryMs)
        RepeatedQueryMs = Get-Median @($samples.Metrics.repeatedQueryAverageMs)
    }
}

$results = foreach ($case in $cases) {
    Invoke-BenchmarkCase -Case $case
}

"| Case | Total median | Total CV | Load median | Execute median | First query | Repeated query | Peak RAM | Env frames | Env copies | Clause attempts | Standardized clauses | Unifications | Fact candidates | Relationship candidates | Relationship pruned |"
"|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
foreach ($result in $results) {
"| {0} | `{1:N2} ms` | `{2:N1}%` | `{3:N2} ms` | `{4:N2} ms` | `{5:N3} ms` | `{6:N3} ms` | `{7:N2} MB` | `{8:N0}` | `{9:N0}` | `{10:N0}` | `{11:N0}` | `{12:N0}` | `{13:N0}` | `{14:N0}` | `{15:N0}` |" -f `
        $result.Case,
        $result.MedianMs,
        $result.TotalCv,
        $result.LoadMs,
        $result.ExecuteMs,
        $result.FirstQueryMs,
        $result.RepeatedQueryMs,
        $result.PeakMb,
        $result.EnvFrames,
        $result.EnvCopies,
        $result.ClauseAttempts,
        $result.StandardizedClauses,
        $result.Unifications,
        $result.FactCandidates,
        $result.RelationshipCandidates,
        $result.RelationshipPruned
}
