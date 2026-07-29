param(
    [int[]] $Counts = @(10000, 100000, 1000000),
    [int] $RepeatedQueries = 20,
    [int] $WarmupRuns = 1,
    [int] $MeasuredRuns = 3,
    [string] $Exe = "",
    [string] $DebugExe = "",
    [string] $CelidaeExe = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exe = if ($Exe) { $Exe } else { Join-Path $root "build\felidae.exe" }
$debugExe = if ($DebugExe) { $DebugExe } else { Join-Path $root "build\felidae_debug.exe" }
$celidaeExe = if ($CelidaeExe) { $CelidaeExe } else { Join-Path $root "build\celidae.exe" }
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Missing $exe. Run .\build.cmd --configuration production first."
}
if ($WarmupRuns -lt 0) { throw "WarmupRuns cannot be negative." }
if ($MeasuredRuns -lt 1) { throw "MeasuredRuns must be at least one." }

function Get-Distribution {
    param(
        [object[]] $Samples,
        [string] $Property
    )
    $values = @($Samples | ForEach-Object { [double]($_.$Property) } |
        Where-Object { -not [double]::IsNaN($_) } | Sort-Object)
    if ($values.Count -eq 0) {
        return [pscustomobject]@{
            Median = [double]::NaN
            P95 = [double]::NaN
            Cv = [double]::NaN
        }
    }
    $middle = [int][math]::Floor($values.Count / 2)
    $median = if (($values.Count % 2) -eq 0) {
        ($values[$middle - 1] + $values[$middle]) / 2.0
    } else {
        $values[$middle]
    }
    $p95Index = [math]::Max(
        0,
        [math]::Min($values.Count - 1, [int][math]::Ceiling($values.Count * 0.95) - 1))
    $mean = ($values | Measure-Object -Average).Average
    $variance = 0.0
    foreach ($value in $values) {
        $difference = $value - $mean
        $variance += $difference * $difference
    }
    $variance /= $values.Count
    [pscustomobject]@{
        Median = $median
        P95 = $values[$p95Index]
        Cv = if ($mean -eq 0.0) { 0.0 } else { [math]::Sqrt($variance) / $mean }
    }
}

function New-LargeFactProgram {
    param([int] $Count)

    $path = Join-Path ([IO.Path]::GetTempPath()) "felidae-large-facts-$Count.fx"
    $writer = [IO.StreamWriter]::new($path, $false, [Text.UTF8Encoding]::new($false), 1MB)
    try {
        for ($i = 0; $i -lt $Count; $i++) {
            $writer.Write('LargeFact(id: ')
            $writer.Write($i)
            $writer.Write(', group: "g')
            $writer.Write($i % 100)
            $writer.Write('", value: ')
            $writer.Write($i * 0.5)
            $writer.WriteLine(')')
        }
    } finally {
        $writer.Dispose()
    }
    return $path
}

function Invoke-MeasuredQuery {
    param(
        [string] $Program,
        [string] $Query,
        [int] $Repeat
    )

    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = $exe
    $process.StartInfo.WorkingDirectory = $root
    $escapedQuery = $Query -replace '"', '\"'
    $process.StartInfo.Arguments =
        "`"$Program`" `"$escapedQuery`" --benchmark-repeat $Repeat --metrics-json"
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true

    $timer = [Diagnostics.Stopwatch]::StartNew()
    [void] $process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(120000)) {
        try { $process.Kill() } catch {}
        throw "Large fact query exceeded the 120 second timeout."
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $timer.Stop()

    if ($process.ExitCode -ne 0) {
        throw "Large fact query failed: $stderr$stdout"
    }
    $match = [regex]::Match($stderr, 'FELIDAE_METRICS (\{.*\})')
    if (-not $match.Success) {
        throw "Large fact query did not emit metrics."
    }
    $metrics = $match.Groups[1].Value | ConvertFrom-Json
    [pscustomobject]@{
        TotalMs = $timer.Elapsed.TotalMilliseconds
        LoadMs = $metrics.loadMs
        FirstQueryMs = $metrics.firstQueryMs
        AverageQueryMs = $metrics.repeatedQueryAverageMs
        FactCandidates = $metrics.runtime.factCandidates
        EnvCopies = $metrics.runtime.environmentCopies
        Unifications = $metrics.runtime.unificationAttempts
        StreamedModuleMs = $metrics.runtime.streamedModuleMicros / 1000.0
        FactRegistrationMs = $metrics.runtime.factRegistrationMicros / 1000.0
    }
}

function Invoke-MeasuredTool {
    param(
        [string] $Executable,
        [string[]] $Arguments
    )
    if (-not (Test-Path -LiteralPath $Executable)) {
        return [pscustomobject]@{ TotalMs = [double]::NaN; PeakMb = [double]::NaN }
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = $Executable
    $process.StartInfo.WorkingDirectory = $root
    $process.StartInfo.Arguments = ($Arguments | ForEach-Object {
        '"' + ($_ -replace '"', '\"') + '"'
    }) -join " "
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $timer = [Diagnostics.Stopwatch]::StartNew()
    [void] $process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $peak = 0L
    while (-not $process.HasExited) {
        $process.Refresh()
        if ($process.WorkingSet64 -gt $peak) { $peak = $process.WorkingSet64 }
        if ($timer.Elapsed.TotalSeconds -ge 180) {
            try { $process.Kill() } catch {}
            throw "Tool exceeded the 180 second scalability timeout."
        }
        Start-Sleep -Milliseconds 5
    }
    [void] $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $timer.Stop()
    if ($process.ExitCode -ne 0) {
        throw "Tool failed with exit code $($process.ExitCode): $stderr"
    }
    [pscustomobject]@{
        TotalMs = $timer.Elapsed.TotalMilliseconds
        PeakMb = $peak / 1MB
    }
}

$results = foreach ($count in ($Counts | Sort-Object -Unique)) {
    $program = New-LargeFactProgram -Count $count
    try {
        $query = "? LargeFact(id: $($count - 1), group: group)"
        for ($warmup = 0; $warmup -lt $WarmupRuns; $warmup++) {
            [void](Invoke-MeasuredQuery -Program $program -Query $query -Repeat $RepeatedQueries)
            [void](Invoke-MeasuredTool -Executable $debugExe -Arguments @($program, "--check-json"))
            [void](Invoke-MeasuredTool -Executable $celidaeExe -Arguments @($program, "--json"))
        }
        $measurements = @()
        $debugMeasurements = @()
        $celidaeMeasurements = @()
        for ($run = 0; $run -lt $MeasuredRuns; $run++) {
            $measurements += Invoke-MeasuredQuery -Program $program -Query $query -Repeat $RepeatedQueries
            $debugMeasurements += Invoke-MeasuredTool -Executable $debugExe -Arguments @($program, "--check-json")
            $celidaeMeasurements += Invoke-MeasuredTool -Executable $celidaeExe -Arguments @($program, "--json")
        }
        $load = Get-Distribution $measurements "LoadMs"
        $firstQuery = Get-Distribution $measurements "FirstQueryMs"
        $warmQuery = Get-Distribution $measurements "AverageQueryMs"
        $streamed = Get-Distribution $measurements "StreamedModuleMs"
        $registration = Get-Distribution $measurements "FactRegistrationMs"
        $debugTime = Get-Distribution $debugMeasurements "TotalMs"
        $debugMemory = Get-Distribution $debugMeasurements "PeakMb"
        $celidaeTime = Get-Distribution $celidaeMeasurements "TotalMs"
        $celidaeMemory = Get-Distribution $celidaeMeasurements "PeakMb"
        $measurement = $measurements[0]
        [pscustomobject]@{
            Facts = $count
            Runs = $MeasuredRuns
            LoadMs = $load.Median
            LoadP95Ms = $load.P95
            LoadCv = $load.Cv
            FirstQueryMs = $firstQuery.Median
            FirstQueryP95Ms = $firstQuery.P95
            FirstQueryCv = $firstQuery.Cv
            QueryAvgMs = $warmQuery.Median
            QueryP95Ms = $warmQuery.P95
            QueryCv = $warmQuery.Cv
            FactCandidates = $measurement.FactCandidates
            EnvCopies = $measurement.EnvCopies
            Unifications = $measurement.Unifications
            StreamedModuleMs = $streamed.Median
            FactRegistrationMs = $registration.Median
            DebugMs = $debugTime.Median
            DebugP95Ms = $debugTime.P95
            DebugCv = $debugTime.Cv
            DebugPeakMb = $debugMemory.P95
            CelidaeMs = $celidaeTime.Median
            CelidaeP95Ms = $celidaeTime.P95
            CelidaeCv = $celidaeTime.Cv
            CelidaePeakMb = $celidaeMemory.P95
        }
    } finally {
        Remove-Item -LiteralPath $program -Force -ErrorAction SilentlyContinue
    }
}

"| Facts | Runs | Load median / P95 / CV | Stream parse/register | Fact registration | First query median / P95 / CV | Repeated query median | Fact candidates | Env copies | Unifications | Debugger median / P95 / CV | Debug RAM P95 | Celidae median / P95 / CV | Celidae RAM P95 |"
"|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
foreach ($result in $results) {
    "| {0:N0} | {1:N0} | `{2:N2} / {3:N2} ms / {4:P2}` | `{5:N2} ms` | `{6:N2} ms` | `{7:N3} / {8:N3} ms / {9:P2}` | `{10:N3} ms` | `{11:N0}` | `{12:N0}` | `{13:N0}` | `{14:N2} / {15:N2} ms / {16:P2}` | `{17:N2} MB` | `{18:N2} / {19:N2} ms / {20:P2}` | `{21:N2} MB` |" -f `
        $result.Facts,
        $result.Runs,
        $result.LoadMs,
        $result.LoadP95Ms,
        $result.LoadCv,
        $result.StreamedModuleMs,
        $result.FactRegistrationMs,
        $result.FirstQueryMs,
        $result.FirstQueryP95Ms,
        $result.FirstQueryCv,
        $result.QueryAvgMs,
        $result.FactCandidates,
        $result.EnvCopies,
        $result.Unifications,
        $result.DebugMs,
        $result.DebugP95Ms,
        $result.DebugCv,
        $result.DebugPeakMb,
        $result.CelidaeMs,
        $result.CelidaeP95Ms,
        $result.CelidaeCv,
        $result.CelidaePeakMb
}

"FELIDAE_LARGE_FACT_BENCHMARK_JSON " + ($results | ConvertTo-Json -Compress)
