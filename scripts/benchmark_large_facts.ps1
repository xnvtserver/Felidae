param(
    [int[]] $Counts = @(10000, 100000, 1000000),
    [int] $RepeatedQueries = 20,
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
        $measurement = Invoke-MeasuredQuery -Program $program -Query $query -Repeat $RepeatedQueries
        $debugMeasurement = Invoke-MeasuredTool -Executable $debugExe -Arguments @($program, "--check-json")
        $celidaeMeasurement = Invoke-MeasuredTool -Executable $celidaeExe -Arguments @($program, "--json")
        [pscustomobject]@{
            Facts = $count
            LoadMs = $measurement.LoadMs
            FirstQueryMs = $measurement.FirstQueryMs
            QueryAvgMs = $measurement.AverageQueryMs
            FactCandidates = $measurement.FactCandidates
            EnvCopies = $measurement.EnvCopies
            Unifications = $measurement.Unifications
            DebugMs = $debugMeasurement.TotalMs
            DebugPeakMb = $debugMeasurement.PeakMb
            CelidaeMs = $celidaeMeasurement.TotalMs
            CelidaePeakMb = $celidaeMeasurement.PeakMb
        }
    } finally {
        Remove-Item -LiteralPath $program -Force -ErrorAction SilentlyContinue
    }
}

"| Facts | Load | First query | Repeated query avg | Fact candidates | Env copies | Unifications | Debugger | Debug RAM | Celidae | Celidae RAM |"
"|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
foreach ($result in $results) {
    "| {0:N0} | `{1:N2} ms` | `{2:N3} ms` | `{3:N3} ms` | `{4:N0}` | `{5:N0}` | `{6:N0}` | `{7:N2} ms` | `{8:N2} MB` | `{9:N2} ms` | `{10:N2} MB` |" -f `
        $result.Facts,
        $result.LoadMs,
        $result.FirstQueryMs,
        $result.QueryAvgMs,
        $result.FactCandidates,
        $result.EnvCopies,
        $result.Unifications,
        $result.DebugMs,
        $result.DebugPeakMb,
        $result.CelidaeMs,
        $result.CelidaePeakMb
}
