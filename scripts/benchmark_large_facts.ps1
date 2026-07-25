param(
    [int[]] $Counts = @(10000, 100000),
    [switch] $IncludeMillion,
    [int] $RepeatedQueries = 20
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exe = Join-Path $root "build\felidae.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Missing $exe. Run .\build.cmd --configuration production first."
}
if ($IncludeMillion -and $Counts -notcontains 1000000) {
    $Counts += 1000000
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
    $process.StartInfo.Arguments = "`"$Program`" --repl --metrics-json"
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.RedirectStandardInput = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true

    $timer = [Diagnostics.Stopwatch]::StartNew()
    [void] $process.Start()
    for ($i = 0; $i -lt $Repeat; $i++) {
        $process.StandardInput.WriteLine($Query)
    }
    $process.StandardInput.WriteLine("exit")
    $process.StandardInput.Close()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
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
        RepeatedQueryMs = $metrics.executionMs
        AverageQueryMs = $metrics.executionMs / $Repeat
        FactCandidates = $metrics.runtime.factCandidates
        EnvCopies = $metrics.runtime.environmentCopies
        Unifications = $metrics.runtime.unificationAttempts
    }
}

$results = foreach ($count in ($Counts | Sort-Object -Unique)) {
    $program = New-LargeFactProgram -Count $count
    try {
        $query = "? LargeFact(id: $($count - 1), group: group)"
        $measurement = Invoke-MeasuredQuery -Program $program -Query $query -Repeat $RepeatedQueries
        [pscustomobject]@{
            Facts = $count
            LoadMs = $measurement.LoadMs
            QueryAvgMs = $measurement.AverageQueryMs
            FactCandidates = $measurement.FactCandidates
            EnvCopies = $measurement.EnvCopies
            Unifications = $measurement.Unifications
        }
    } finally {
        Remove-Item -LiteralPath $program -Force -ErrorAction SilentlyContinue
    }
}

"| Facts | Load | Repeated query avg | Fact candidates | Env copies | Unifications |"
"|---:|---:|---:|---:|---:|---:|"
foreach ($result in $results) {
    "| {0:N0} | `{1:N2} ms` | `{2:N3} ms` | `{3:N0}` | `{4:N0}` | `{5:N0}` |" -f `
        $result.Facts,
        $result.LoadMs,
        $result.QueryAvgMs,
        $result.FactCandidates,
        $result.EnvCopies,
        $result.Unifications
}
